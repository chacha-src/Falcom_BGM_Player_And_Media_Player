#include "OxzLossless.h"
#include "OxzMedia.h"
#define NOMINMAX
#define FLAC__NO_DLL
#include "flac/stream_encoder.h"
#include "flac/stream_decoder.h"
#include "../codec/ALACEncoder.h"
#include "../codec/ALACDecoder.h"
#include "../codec/ALACBitUtilities.h"
#undef TTA_ALIGNED
#define TTA_ALIGNED(n) __declspec(align(n))
#include "../codec/libtta.h"
#undef near
#undef far
#include <string.h>
#include <vector>
#include <stdlib.h>
#include <immintrin.h>

static uint32_t U32(const uint8_t* p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t U16(const uint8_t* p)
{
	return (uint16_t)(p[0] | (p[1] << 8));
}

static void P32(std::vector<uint8_t>& o, uint32_t v)
{
	o.push_back((uint8_t)v);
	o.push_back((uint8_t)(v >> 8));
	o.push_back((uint8_t)(v >> 16));
	o.push_back((uint8_t)(v >> 24));
}

struct PcmView {
	int channels;
	int kind;
	size_t pcmOff;
	size_t pcmBytes;
	size_t prefix;
	size_t suffixOff;
	size_t suffix;
};

static int TextByte(uint8_t c)
{
	return c == 9 || c == 10 || c == 13 || (c >= 32 && c < 127);
}

static int LooksLikeText(const uint8_t* p, size_t n)
{
	size_t i, text = 0, utf = 0, samples, use;
	use = n > 16384 ? 16384 : n;
	if (use < 64) return 0;
	for (i = 0; i < use; ++i) if (TextByte(p[i])) text++;
	if (text * 5 >= use * 4) return 1;
	samples = use / 2;
	for (i = 0; i + 1 < use; i += 2) {
		if ((p[i + 1] == 0 && TextByte(p[i])) || (p[i] == 0 && TextByte(p[i + 1]))) utf++;
	}
	return samples && utf * 4 >= samples * 3;
}

static int CalmPcm(const uint8_t* p, size_t n, int* channels)
{
	const int16_t* s;
	size_t samples, use, i;
	long long amp = 0, mono = 0, ster = 0, pair = 0;
	if (n < 4096 || (n & 1)) return 0;
	s = (const int16_t*)p;
	samples = n / 2;
	use = samples > 8192 ? 8192 : samples;
	for (i = 0; i + 1 < use; ++i) {
		int a = s[i];
		int b = s[i + 1];
		int da = a < 0 ? -a : a;
		amp += da;
		mono += a > b ? a - b : b - a;
	}
	if (amp < 64ll * (long long)use && mono * 2 < (long long)use * 64) {
		*channels = 1;
		return 1;
	}
	if (amp <= 0 || mono * 4 >= amp * 3) return 0;
	*channels = 1;
	if ((n % 4) == 0 && use > 8) {
		size_t m = use > 4000 ? 4000 : use;
		for (i = 0; i + 2 < m; ++i) {
			int a = s[i], b = s[i + 1], c = s[i + 2];
			ster += a > c ? a - c : c - a;
			pair += a > b ? a - b : b - a;
		}
		if (ster > 0 && ster * 2 < pair) *channels = 2;
	}
	return 1;
}

static int DescribePcm(const uint8_t* p, size_t n, PcmView* v)
{
	memset(v, 0, sizeof(*v));
	if (!p || n < 4096) return 0;
	if (n >= 12 && memcmp(p, "RIFF", 4) == 0 && memcmp(p + 8, "WAVE", 4) == 0) {
		size_t pos = 12;
		int ch = 0, bits = 0, fmt = 0, haveFmt = 0, haveData = 0;
		size_t dataOff = 0, dataLen = 0;
		while (pos + 8 <= n) {
			uint32_t sz = U32(p + pos + 4);
			if ((uint64_t)pos + 8 + sz > n) break;
			if (memcmp(p + pos, "fmt ", 4) == 0 && sz >= 16) {
				fmt = U16(p + pos + 8);
				ch = U16(p + pos + 10);
				bits = U16(p + pos + 22);
				haveFmt = 1;
			} else if (memcmp(p + pos, "data", 4) == 0) {
				dataOff = pos + 8;
				dataLen = sz;
				haveData = 1;
				break;
			}
			pos += 8u + sz + (sz & 1u);
		}
		if (haveFmt && haveData && fmt == 1 && bits == 16 && (ch == 1 || ch == 2) && dataLen >= 4096 && (dataLen & 1) == 0 && dataOff + dataLen <= n) {
			v->channels = ch;
			v->kind = 1;
			v->pcmOff = dataOff;
			v->pcmBytes = dataLen;
			v->prefix = dataOff;
			v->suffixOff = dataOff + dataLen;
			v->suffix = n - v->suffixOff;
			return 1;
		}
		return 0;
	}
	if (p[0] == 'M' && p[1] == 'Z') return 0;
	if (p[0] == 'P' && p[1] == 'K') return 0;
	if (p[0] == 'B' && p[1] == 'M') return 0;
	if (p[0] == 'O' && p[1] == 'X' && p[2] == 'Z') return 0;
	if (p[0] == 0x37 && p[1] == 0x7A) return 0;
	if (LooksLikeText(p, n)) return 0;
	{
		size_t pcm = n & ~1u;
		int ch = 1;
		if (!CalmPcm(p, pcm > 16384 ? 16384 : pcm, &ch)) return 0;
		v->channels = ch;
		v->kind = 0;
		v->pcmOff = 0;
		v->pcmBytes = pcm;
		v->prefix = 0;
		v->suffixOff = pcm;
		v->suffix = n - pcm;
		return 1;
	}
}

int OxzPcmMaybe(const uint8_t* p, size_t n)
{
	PcmView v;
	return DescribePcm(p, n, &v);
}

static void ScoreFlacAlign(const uint8_t* p, size_t avail, long long* outAmp, long long* outMono, long long* outSter)
{
	long long amp = 0, m1 = 0, m2 = 0, s1 = 0, s2 = 0;
	size_t totalSamples = avail / 2;
	unsigned winCount = 4;
	size_t winSamples = 256;
	unsigned w;
	if (totalSamples < 8) {
		*outAmp = 0;
		*outMono = 1;
		*outSter = 1;
		return;
	}
	if (totalSamples < winSamples * winCount) {
		winCount = 1;
		winSamples = totalSamples > 1024 ? 1024 : totalSamples;
	}
	for (w = 0; w < winCount; ++w) {
		size_t start = (winCount == 1) ? 0 : ((totalSamples - winSamples) * (w + 1)) / (winCount + 1);
		const uint8_t* sp = p + start * 2;
		size_t i;
		int16_t s[1024];
		memcpy(s, sp, winSamples * 2);
		for (i = 4; i < winSamples; ++i) {
			int a = s[i];
			int d1 = a - (int)s[i - 1];
			int d2 = a - 2 * (int)s[i - 1] + (int)s[i - 2];
			int q1 = a - (int)s[i - 2];
			int q2 = a - 2 * (int)s[i - 2] + (int)s[i - 4];
			amp += a < 0 ? -a : a;
			m1 += d1 < 0 ? -d1 : d1;
			m2 += d2 < 0 ? -d2 : d2;
			s1 += q1 < 0 ? -q1 : q1;
			s2 += q2 < 0 ? -q2 : q2;
		}
	}
	*outAmp = amp;
	*outMono = m1 < m2 ? m1 : m2;
	*outSter = s1 < s2 ? s1 : s2;
}

static int DescribeFlacAny(const uint8_t* p, size_t n, int scout, PcmView* v)
{
	long long amp0 = 0, mono0 = 0, ster0 = 0;
	long long bestRes, bestAmp;
	size_t off = 0;
	int ch = 1;
	size_t pcm;
	if (DescribePcm(p, n, v)) return 1;
	memset(v, 0, sizeof(*v));
	if (!p || n < 512 || n > 64u * 1024u * 1024u) return 0;
	ScoreFlacAlign(p, n, &amp0, &mono0, &ster0);
	bestAmp = amp0;
	bestRes = mono0;
	if (n >= 16 && ster0 * 10 < bestRes * 9) {
		bestRes = ster0;
		ch = 2;
	}
	if (n >= 520) {
		long long amp1 = 0, mono1 = 0, ster1 = 0;
		ScoreFlacAlign(p + 1, n - 1, &amp1, &mono1, &ster1);
		if (amp1 > 0 && mono1 * 10 < bestRes * 9) {
			bestRes = mono1;
			bestAmp = amp1;
			off = 1;
			ch = 1;
		}
		if (n >= 20 && amp1 > 0 && ster1 * 10 < bestRes * 9) {
			bestRes = ster1;
			bestAmp = amp1;
			off = 1;
			ch = 2;
		}
	}
	if (scout) {
		if (bestAmp <= 0 || bestRes > bestAmp) return 0;
	}
	pcm = (n - off) & ~(size_t)(ch == 2 ? 3u : 1u);
	if (pcm < (size_t)ch * 2u) return 0;
	v->channels = ch;
	v->kind = 0;
	v->pcmOff = off;
	v->pcmBytes = pcm;
	v->prefix = off;
	v->suffixOff = off + pcm;
	v->suffix = n - off - pcm;
	return 1;
}

static int WrapAudio(int codec, const PcmView* v, const uint8_t* in, const std::vector<uint8_t>& body, size_t original, std::vector<uint8_t>& out)
{
	std::vector<uint8_t> o;
	if (body.empty() || !v->pcmBytes) return 0;
	try {
		o.push_back('O'); o.push_back('X'); o.push_back('A'); o.push_back('1');
		o.push_back((uint8_t)codec);
		o.push_back((uint8_t)v->channels);
		o.push_back((uint8_t)v->kind);
		o.push_back(0);
		P32(o, (uint32_t)v->pcmBytes);
		P32(o, (uint32_t)v->prefix);
		P32(o, (uint32_t)v->suffix);
		if (v->prefix) o.insert(o.end(), in, in + v->prefix);
		if (v->suffix) o.insert(o.end(), in + v->suffixOff, in + v->suffixOff + v->suffix);
		o.insert(o.end(), body.begin(), body.end());
	} catch (...) { return 0; }
	if (o.size() >= original) return 0;
	out.swap(o);
	return 1;
}

struct AudioHead {
	int codec, channels, kind;
	uint32_t pcmBytes, prefix, suffix;
	size_t body;
};

static int ParseAudio(const uint8_t* in, size_t n, AudioHead* h)
{
	if (!in || n < 20 || in[0] != 'O' || in[1] != 'X' || in[2] != 'A' || in[3] != '1') return 0;
	h->codec = in[4];
	h->channels = in[5];
	h->kind = in[6];
	h->pcmBytes = U32(in + 8);
	h->prefix = U32(in + 12);
	h->suffix = U32(in + 16);
	if (h->channels != 1 && h->channels != 2) return 0;
	if ((uint64_t)h->prefix + h->pcmBytes + h->suffix == 0) return 0;
	if (20u + (uint64_t)h->prefix + h->suffix > n) return 0;
	h->body = 20u + h->prefix + h->suffix;
	return 1;
}

static int RestoreAudio(const AudioHead* h, const uint8_t* in, const uint8_t* pcm, uint8_t* dst, size_t dstLen)
{
	if ((uint64_t)h->prefix + h->pcmBytes + h->suffix != dstLen) return 0;
	if (h->prefix) memcpy(dst, in + 20, h->prefix);
	memcpy(dst + h->prefix, pcm, h->pcmBytes);
	if (h->suffix) memcpy(dst + h->prefix + h->pcmBytes, in + 20 + h->prefix, h->suffix);
	return 1;
}

struct FlacBuf {
	std::vector<uint8_t>* v;
	size_t pos;
};

static FLAC__StreamEncoderWriteStatus FlacWrite(const FLAC__StreamEncoder*, const FLAC__byte buffer[], size_t bytes, uint32_t, uint32_t, void* client)
{
	FlacBuf* f = (FlacBuf*)client;
	try {
		if (f->pos + bytes > f->v->size()) f->v->resize(f->pos + bytes);
	} catch (...) { return FLAC__STREAM_ENCODER_WRITE_STATUS_FATAL_ERROR; }
	memcpy(f->v->data() + f->pos, buffer, bytes);
	f->pos += bytes;
	return FLAC__STREAM_ENCODER_WRITE_STATUS_OK;
}

static FLAC__StreamEncoderSeekStatus FlacSeek(const FLAC__StreamEncoder*, FLAC__uint64 absolute, void* client)
{
	FlacBuf* f = (FlacBuf*)client;
	f->pos = (size_t)absolute;
	return FLAC__STREAM_ENCODER_SEEK_STATUS_OK;
}

static FLAC__StreamEncoderTellStatus FlacTell(const FLAC__StreamEncoder*, FLAC__uint64* absolute, void* client)
{
	FlacBuf* f = (FlacBuf*)client;
	*absolute = f->pos;
	return FLAC__STREAM_ENCODER_TELL_STATUS_OK;
}

struct TlsFlac {
	FLAC__StreamEncoder* enc;
	FLAC__StreamDecoder* dec;
	std::vector<FLAC__int32> tmp;
	TlsFlac() : enc(NULL), dec(NULL) {}
	~TlsFlac() {
		if (enc) FLAC__stream_encoder_delete(enc);
		if (dec) FLAC__stream_decoder_delete(dec);
	}
};

static TlsFlac& FlacTls(void)
{
	static thread_local TlsFlac tls;
	return tls;
}

int OxzFlacCompress(const uint8_t* in, size_t n, std::vector<uint8_t>& out)
{
	PcmView view;
	TlsFlac& tf = FlacTls();
	FLAC__StreamEncoder* enc;
	FlacBuf buf;
	std::vector<uint8_t> body;
	size_t samples, done = 0;
	int level;
	int scout = OxzEncodeScout();
	if (!DescribeFlacAny(in, n, scout, &view)) return 0;
	samples = view.pcmBytes / 2 / (size_t)view.channels;
	if (!samples) return 0;
	if (!tf.enc) tf.enc = FLAC__stream_encoder_new();
	enc = tf.enc;
	if (!enc) return 0;
	level = scout ? 0 : (OxzEncodeLevel() * 8 + 9) / 10;
	if (level < 0) level = 0;
	if (level > 8) level = 8;
	FLAC__stream_encoder_set_verify(enc, false);
	FLAC__stream_encoder_set_channels(enc, (unsigned)view.channels);
	FLAC__stream_encoder_set_bits_per_sample(enc, 16);
	FLAC__stream_encoder_set_sample_rate(enc, 44100);
	FLAC__stream_encoder_set_compression_level(enc, (unsigned)level);
	FLAC__stream_encoder_set_blocksize(enc, 4096);
	FLAC__stream_encoder_set_total_samples_estimate(enc, samples);
	try { body.reserve(n / 2 + 256); } catch (...) {}
	buf.v = &body;
	buf.pos = 0;
	if (FLAC__stream_encoder_init_stream(enc, FlacWrite, FlacSeek, FlacTell, NULL, &buf) != FLAC__STREAM_ENCODER_INIT_STATUS_OK) {
		FLAC__stream_encoder_delete(enc);
		tf.enc = FLAC__stream_encoder_new();
		enc = tf.enc;
		if (!enc) return 0;
		FLAC__stream_encoder_set_verify(enc, false);
		FLAC__stream_encoder_set_channels(enc, (unsigned)view.channels);
		FLAC__stream_encoder_set_bits_per_sample(enc, 16);
		FLAC__stream_encoder_set_sample_rate(enc, 44100);
		FLAC__stream_encoder_set_compression_level(enc, (unsigned)level);
		FLAC__stream_encoder_set_blocksize(enc, 4096);
		FLAC__stream_encoder_set_total_samples_estimate(enc, samples);
		if (FLAC__stream_encoder_init_stream(enc, FlacWrite, FlacSeek, FlacTell, NULL, &buf) != FLAC__STREAM_ENCODER_INIT_STATUS_OK)
			return 0;
	}
	try {
		if (tf.tmp.size() < 4096u * (unsigned)view.channels)
			tf.tmp.resize(4096u * (unsigned)view.channels);
	} catch (...) { FLAC__stream_encoder_finish(enc); return 0; }
	while (done < samples) {
		unsigned count = (unsigned)(samples - done);
		unsigned total, i = 0;
		const uint8_t* src;
		FLAC__int32* dst32 = tf.tmp.data();
		if (count > 4096) count = 4096;
		total = count * (unsigned)view.channels;
		src = in + view.pcmOff + done * (size_t)view.channels * 2;
		for (; i + 16 <= total; i += 16) {
			__m128i s0 = _mm_loadu_si128((const __m128i*)(src + (size_t)i * 2));
			__m128i s1 = _mm_loadu_si128((const __m128i*)(src + (size_t)i * 2 + 16));
			_mm256_storeu_si256((__m256i*)(dst32 + i), _mm256_cvtepi16_epi32(s0));
			_mm256_storeu_si256((__m256i*)(dst32 + i + 8), _mm256_cvtepi16_epi32(s1));
		}
		for (; i < total; ++i) {
			int16_t sv;
			memcpy(&sv, src + (size_t)i * 2, 2);
			dst32[i] = (FLAC__int32)sv;
		}
		if (!FLAC__stream_encoder_process_interleaved(enc, dst32, count)) {
			FLAC__stream_encoder_finish(enc);
			return 0;
		}
		done += count;
		OxzPulseEncode(done * (size_t)view.channels * 2, view.pcmBytes);
	}
	if (!FLAC__stream_encoder_finish(enc)) return 0;
	return WrapAudio(1, &view, in, body, n, out);
}

struct FlacIn {
	const uint8_t* p;
	size_t n, pos;
	uint8_t* outPtr;
	size_t outCap, outPos;
	int channels;
	int fail;
};

static FLAC__StreamDecoderReadStatus FlacRead(const FLAC__StreamDecoder*, FLAC__byte buffer[], size_t* bytes, void* client)
{
	FlacIn* f = (FlacIn*)client;
	size_t n;
	if (!bytes || *bytes == 0) return FLAC__STREAM_DECODER_READ_STATUS_ABORT;
	if (f->pos >= f->n) { *bytes = 0; return FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM; }
	n = f->n - f->pos;
	if (n > *bytes) n = *bytes;
	memcpy(buffer, f->p + f->pos, n);
	f->pos += n;
	*bytes = n;
	return FLAC__STREAM_DECODER_READ_STATUS_CONTINUE;
}

static FLAC__StreamDecoderSeekStatus FlacDSeek(const FLAC__StreamDecoder*, FLAC__uint64 absolute, void* client)
{
	FlacIn* f = (FlacIn*)client;
	if (absolute > f->n) return FLAC__STREAM_DECODER_SEEK_STATUS_ERROR;
	f->pos = (size_t)absolute;
	return FLAC__STREAM_DECODER_SEEK_STATUS_OK;
}

static FLAC__StreamDecoderTellStatus FlacDTell(const FLAC__StreamDecoder*, FLAC__uint64* absolute, void* client)
{
	FlacIn* f = (FlacIn*)client;
	*absolute = f->pos;
	return FLAC__STREAM_DECODER_TELL_STATUS_OK;
}

static FLAC__StreamDecoderLengthStatus FlacLen(const FLAC__StreamDecoder*, FLAC__uint64* length, void* client)
{
	FlacIn* f = (FlacIn*)client;
	*length = f->n;
	return FLAC__STREAM_DECODER_LENGTH_STATUS_OK;
}

static FLAC__bool FlacEof(const FLAC__StreamDecoder*, void* client)
{
	FlacIn* f = (FlacIn*)client;
	return f->pos >= f->n;
}

static FLAC__StreamDecoderWriteStatus FlacOut(const FLAC__StreamDecoder*, const FLAC__Frame* frame, const FLAC__int32* const buffer[], void* client)
{
	FlacIn* f = (FlacIn*)client;
	unsigned ch, i = 0, n;
	size_t bytes;
	uint8_t* d;
	if (!frame || frame->header.channels != (unsigned)f->channels || frame->header.bits_per_sample != 16) { f->fail = 1; return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT; }
	ch = frame->header.channels;
	n = frame->header.blocksize;
	bytes = (size_t)n * ch * 2;
	if (f->outPos >= f->outCap) return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
	if (bytes > f->outCap - f->outPos) {
		n = (unsigned)((f->outCap - f->outPos) / (ch * 2));
		bytes = (size_t)n * ch * 2;
	}
	d = f->outPtr + f->outPos;
	if (ch == 1) {
		const FLAC__int32* p0 = buffer[0];
		for (; i + 16 <= n; i += 16) {
			__m256i a = _mm256_loadu_si256((const __m256i*)(p0 + i));
			__m256i b = _mm256_loadu_si256((const __m256i*)(p0 + i + 8));
			__m256i pk = _mm256_permute4x64_epi64(_mm256_packs_epi32(a, b), 0xD8);
			_mm256_storeu_si256((__m256i*)(d + (size_t)i * 2), pk);
		}
		for (; i < n; ++i) {
			int16_t s = (int16_t)p0[i];
			memcpy(d + (size_t)i * 2, &s, 2);
		}
	} else {
		const FLAC__int32* pL = buffer[0];
		const FLAC__int32* pR = buffer[1];
		__m256i mask = _mm256_set1_epi32(0x0000FFFF);
		for (; i + 8 <= n; i += 8) {
			__m256i vl = _mm256_and_si256(_mm256_loadu_si256((const __m256i*)(pL + i)), mask);
			__m256i vr = _mm256_slli_epi32(_mm256_loadu_si256((const __m256i*)(pR + i)), 16);
			_mm256_storeu_si256((__m256i*)(d + (size_t)i * 4), _mm256_or_si256(vl, vr));
		}
		for (; i < n; ++i) {
			int16_t sl = (int16_t)pL[i];
			int16_t sr = (int16_t)pR[i];
			memcpy(d + (size_t)i * 4, &sl, 2);
			memcpy(d + (size_t)i * 4 + 2, &sr, 2);
		}
	}
	f->outPos += bytes;
	return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}

static void FlacErr(const FLAC__StreamDecoder*, FLAC__StreamDecoderErrorStatus, void* client)
{
	((FlacIn*)client)->fail = 1;
}

int OxzFlacDecompress(const uint8_t* in, size_t n, uint8_t* dst, size_t dstLen)
{
	AudioHead h;
	TlsFlac& tf = FlacTls();
	FLAC__StreamDecoder* dec;
	FlacIn f;
	int ok;
	if (!dst || !ParseAudio(in, n, &h) || h.codec != 1) return 0;
	if ((uint64_t)h.prefix + h.pcmBytes + h.suffix != dstLen) return 0;
	if (!tf.dec) tf.dec = FLAC__stream_decoder_new();
	dec = tf.dec;
	if (!dec) return 0;
	f.p = in + h.body;
	f.n = n - h.body;
	f.pos = 0;
	f.outPtr = dst + h.prefix;
	f.outCap = h.pcmBytes;
	f.outPos = 0;
	f.channels = h.channels;
	f.fail = 0;
	if (FLAC__stream_decoder_init_stream(dec, FlacRead, FlacDSeek, FlacDTell, FlacLen, FlacEof, FlacOut, NULL, FlacErr, &f) != FLAC__STREAM_DECODER_INIT_STATUS_OK) {
		FLAC__stream_decoder_delete(dec);
		tf.dec = FLAC__stream_decoder_new();
		dec = tf.dec;
		if (!dec) return 0;
		if (FLAC__stream_decoder_init_stream(dec, FlacRead, FlacDSeek, FlacDTell, FlacLen, FlacEof, FlacOut, NULL, FlacErr, &f) != FLAC__STREAM_DECODER_INIT_STATUS_OK)
			return 0;
	}
	ok = FLAC__stream_decoder_process_until_end_of_stream(dec) && !f.fail && f.outPos == h.pcmBytes;
	FLAC__stream_decoder_finish(dec);
	if (!ok) return 0;
	if (h.prefix) memcpy(dst, in + 20, h.prefix);
	if (h.suffix) memcpy(dst + h.prefix + h.pcmBytes, in + 20 + h.prefix, h.suffix);
	return 1;
}

int OxzAlacCompress(const uint8_t* in, size_t n, std::vector<uint8_t>& out)
{
	PcmView view;
	AudioFormatDescription inFmt, outFmt;
	ALACEncoder enc;
	std::vector<uint8_t> cookie, frame, body;
	uint32_t cookieSize, leftSamples;
	const uint8_t* pcm;
	if (!DescribePcm(in, n, &view)) return 0;
	if (view.pcmBytes / 2 / (size_t)view.channels > 0xFFFFFFFFu) return 0;
	memset(&inFmt, 0, sizeof(inFmt));
	memset(&outFmt, 0, sizeof(outFmt));
	inFmt.mSampleRate = 44100;
	inFmt.mFormatID = kALACFormatLinearPCM;
	inFmt.mFormatFlags = kALACFormatFlagIsSignedInteger;
	inFmt.mBytesPerPacket = (uint32_t)view.channels * 2;
	inFmt.mFramesPerPacket = 1;
	inFmt.mBytesPerFrame = (uint32_t)view.channels * 2;
	inFmt.mChannelsPerFrame = (uint32_t)view.channels;
	inFmt.mBitsPerChannel = 16;
	outFmt = inFmt;
	outFmt.mFormatID = kALACFormatAppleLossless;
	outFmt.mFormatFlags = 1;
	outFmt.mFramesPerPacket = 4096;
	outFmt.mBytesPerPacket = 0;
	enc.SetFrameSize(4096);
	enc.SetFastMode(true);
	if (enc.InitializeEncoder(outFmt) != 0) return 0;
	cookieSize = enc.GetMagicCookieSize((uint32_t)view.channels);
	try { cookie.resize(cookieSize ? cookieSize : 1); frame.resize(4096u * (unsigned)view.channels * 8u + 64u); }
	catch (...) { return 0; }
	enc.GetMagicCookie(cookie.data(), &cookieSize);
	try { cookie.resize(cookieSize); body.clear(); P32(body, cookieSize); body.insert(body.end(), cookie.begin(), cookie.end()); }
	catch (...) { return 0; }
	pcm = in + view.pcmOff;
	leftSamples = (uint32_t)(view.pcmBytes / 2 / (size_t)view.channels);
	while (leftSamples) {
		uint32_t take = leftSamples > 4096 ? 4096 : leftSamples;
		int32_t bytes = (int32_t)(take * (uint32_t)view.channels * 2);
		int32_t produced = bytes;
		if (enc.Encode(inFmt, outFmt, (unsigned char*)pcm, frame.data(), &produced) != 0 || produced <= 0) return 0;
		try {
			P32(body, (uint32_t)produced);
			P32(body, take);
			body.insert(body.end(), frame.begin(), frame.begin() + produced);
		} catch (...) { return 0; }
		pcm += (size_t)take * (size_t)view.channels * 2;
		leftSamples -= take;
		OxzPulseEncode(view.pcmBytes - (size_t)leftSamples * (size_t)view.channels * 2, view.pcmBytes);
	}
	return WrapAudio(2, &view, in, body, n, out);
}

int OxzAlacDecompress(const uint8_t* in, size_t n, uint8_t* dst, size_t dstLen)
{
	AudioHead h;
	ALACDecoder dec;
	std::vector<uint8_t> pcm;
	const uint8_t* p;
	size_t left;
	uint32_t cookieSize, filled = 0;
	if (!ParseAudio(in, n, &h) || h.codec != 2) return 0;
	p = in + h.body;
	left = n - h.body;
	if (left < 4) return 0;
	cookieSize = U32(p);
	p += 4;
	left -= 4;
	if (cookieSize > left) return 0;
	if (dec.Init((void*)p, cookieSize) != 0) return 0;
	p += cookieSize;
	left -= cookieSize;
	try { pcm.resize(h.pcmBytes); }
	catch (...) { return 0; }
	while (filled < h.pcmBytes) {
		uint32_t frameBytes, samples, got = 0;
		BitBuffer bits;
		if (left < 8) return 0;
		frameBytes = U32(p);
		samples = U32(p + 4);
		p += 8;
		left -= 8;
		if (!samples || frameBytes > left) return 0;
		if (filled + (uint64_t)samples * (unsigned)h.channels * 2 > h.pcmBytes) return 0;
		BitBufferInit(&bits, (uint8_t*)p, frameBytes);
		if (dec.Decode(&bits, pcm.data() + filled, samples, (uint32_t)h.channels, &got) != 0) return 0;
		if (got != samples) return 0;
		filled += samples * (uint32_t)h.channels * 2;
		p += frameBytes;
		left -= frameBytes;
		OxzPulseDecode(filled);
	}
	return RestoreAudio(&h, in, pcm.data(), dst, dstLen);
}

struct TtaMem {
	TTA_io_callback cb;
	std::vector<uint8_t> buf;
	const uint8_t* readOnly;
	size_t readN;
	size_t pos;
	int writable;
};

static TTAint32 __stdcall TtaRead(TTA_io_callback* io, TTAuint8* p, TTAuint32 n)
{
	TtaMem* m = (TtaMem*)io;
	size_t have, take;
	if (m->pos >= m->readN) return 0;
	have = m->readN - m->pos;
	take = have > n ? n : have;
	memcpy(p, m->readOnly + m->pos, take);
	if (take < n) memset(p + take, 0, n - take);
	m->pos += take;
	return (TTAint32)n;
}

static TTAint32 __stdcall TtaWrite(TTA_io_callback* io, TTAuint8* p, TTAuint32 n)
{
	TtaMem* m = (TtaMem*)io;
	if (!m->writable) return 0;
	try {
		if (m->pos + n > m->buf.size()) m->buf.resize(m->pos + n);
	} catch (...) { return 0; }
	memcpy(m->buf.data() + m->pos, p, n);
	m->pos += n;
	m->readOnly = m->buf.data();
	m->readN = m->buf.size();
	return (TTAint32)n;
}

static TTAint64 __stdcall TtaSeek(TTA_io_callback* io, TTAint64 off)
{
	TtaMem* m = (TtaMem*)io;
	if (off < 0) return -1;
	if (m->writable) {
		try { if ((size_t)off > m->buf.size()) m->buf.resize((size_t)off); }
		catch (...) { return -1; }
		m->readOnly = m->buf.data();
		m->readN = m->buf.size();
	} else if ((size_t)off > m->readN) return -1;
	m->pos = (size_t)off;
	return off;
}

static void __stdcall TtaPulse(TTAuint32, TTAuint32 fnum, TTAuint32 frames)
{
	if (frames) OxzPulseEncode(fnum, frames);
}

int OxzTtaCompress(const uint8_t* in, size_t n, std::vector<uint8_t>& out)
{
	PcmView view;
	TtaMem mem;
	TTA_info info;
	if (!DescribePcm(in, n, &view)) return 0;
	if (view.pcmBytes > 0xFFFFFFFFu) return 0;
	if (view.pcmBytes / 2 / (size_t)view.channels > 0xFFFFFFFFu) return 0;
	memset(&mem, 0, sizeof(mem));
	mem.cb.read = TtaRead;
	mem.cb.write = TtaWrite;
	mem.cb.seek = TtaSeek;
	mem.writable = 1;
	try {
		tta::tta_encoder enc(&mem.cb);
		info.format = TTA_FORMAT_SIMPLE;
		info.nch = (TTAuint32)view.channels;
		info.bps = 16;
		info.sps = 44100;
		info.samples = (TTAuint32)(view.pcmBytes / 2 / (size_t)view.channels);
		enc.init_set_info(&info, 0);
		enc.process_stream((TTAuint8*)(in + view.pcmOff), (TTAuint32)view.pcmBytes, TtaPulse);
		enc.finalize();
	} catch (...) { return 0; }
	return WrapAudio(3, &view, in, mem.buf, n, out);
}

int OxzTtaDecompress(const uint8_t* in, size_t n, uint8_t* dst, size_t dstLen)
{
	AudioHead h;
	TtaMem mem;
	std::vector<uint8_t> pcm;
	TTA_info info;
	int got;
	if (!ParseAudio(in, n, &h) || h.codec != 3) return 0;
	memset(&mem, 0, sizeof(mem));
	mem.cb.read = TtaRead;
	mem.cb.write = TtaWrite;
	mem.cb.seek = TtaSeek;
	mem.readOnly = in + h.body;
	mem.readN = n - h.body;
	mem.writable = 0;
	try { pcm.resize(h.pcmBytes); }
	catch (...) { return 0; }
	try {
		tta::tta_decoder dec(&mem.cb);
		dec.init_get_info(&info, 0);
		got = dec.process_stream(pcm.data(), (TTAuint32)pcm.size(), NULL);
	} catch (...) { return 0; }
	if (got <= 0) return 0;
	if ((uint64_t)got * (unsigned)h.channels * 2 < h.pcmBytes) return 0;
	return RestoreAudio(&h, in, pcm.data(), dst, dstLen);
}
