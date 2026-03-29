#pragma once

#include <cstdint>
#include <vector>
#include <cstring>

// ストリーミング用 PCM アップスケール（レート変換・チャンネル展開・ビット拡張）
// 内部は float インターリーブで保持し、4 点 Catmull-Rom 補間でリサンプリングする。
class AudioUpscaler {
public:
	AudioUpscaler();

	void Configure(int srcRate, int srcCh, int srcBits,
		int dstRate, int dstCh, int dstBits);

	void Reset();

	// デコード済みソース PCM（インターリーブ）を内部 FIFO に積む
	void PushInterleaved(const uint8_t* pcm, int byteCount);

	// 必要なら true。呼び出し側はデコードを続ける。
	bool NeedsMoreInput() const;

	// 次の Push に推奨するソースバイト数（概算・マージン付き）
	int SuggestInputBytes(int dstBytesRemaining) const;

	// インターリーブ出力を dst に書き込む。戻り値は書き込んだバイト数（<= dstCapacity）
	int PullInterleaved(uint8_t* dst, int dstCapacity);

	bool IsActive() const { return m_active; }

private:
	void EnsureConfigured() const;
	static void PcmToFloat(const uint8_t* p, int nFrames, int ch, int bits, std::vector<float>& out);
	static int FloatToPcm(const float* interleaved, int nFrames, int ch, int bits, uint8_t* dst);

	float SampleInput(int ch, double posFrames) const;
	void BuildOutputFrame(double pos, float* dstCh) const;

	int m_srcRate = 44100;
	int m_srcCh = 2;
	int m_srcBits = 16;
	int m_dstRate = 44100;
	int m_dstCh = 2;
	int m_dstBits = 16;
	bool m_active = false;

	std::vector<float> m_fifo; // インターリーブ float, サイズ = m_srcCh * frames
	double m_readPos = 0.0;    // fifo 先頭からのフレーム位置（小数）

	std::vector<float> m_scratchFrame;
};

// グローバル（oggDlg / oggDlg_ds から参照）
extern AudioUpscaler g_audioUpscaler;
extern int g_ds_pcm_ch;
extern int g_ds_pcm_rate;
extern int g_ds_pcm_bits;
extern int g_pcm_upscale_active; // 0/1: play() でソース≠DS形式のとき 1
// DirectSound セカンダリ＆ bufwav3 リングのバイト長（チャンネル/ビット増で伸ばし、再生時間をステレオ16基準に近づける）
extern ULONG g_ds_buffer_bytes;

void ResetAudioUpscalerPipeline();
void ConfigurePlaybackOutputAndUpscaler();
int SpeakerLayoutToOutChannels(int layout); // 0=2ch 1=2.1 2=4ch 3=5.1 4=7.1（5=マッピングなしは Configure 側で wavch を使用）
// DirectSound 用: マスクのビット数が常に outCh と一致する（4ch で FC 混入5マスクによる不一致を防ぐ）
std::uint32_t DirectSoundChannelMaskForOutput(int outCh, int speaker_layout);
