#pragma warning(disable:4786)
#include <stdio.h>
#include "sequencer.hpp"


#include <cassert>
#include <cstring>

#include <algorithm>
#include <stdexcept>

//namespace midisequencer{//í by Kobarin
    //namespace{//í by Kobarin
        class load_error:public std::runtime_error{
        public:
            load_error(const std::string& s):std::runtime_error(s){}
        };
        /* ogg の MidiStatusRank と同じ。同じ tick では SysEx / CC / PC をノートより前。
           バンクが別トラックだと、ノートが先に鳴って PC 固定（programs.txt と同じ）になる。 */
        static int midi_setup_rank(uint_least32_t msg)
        {
            const unsigned st = msg & 0xFFu;
            if (st == 0xFFu) return 0;
            if (st == 0xF0u) return 5;
            const unsigned type = st & 0xF0u;
            if (type == 0xB0u) {
                const unsigned cc = (msg >> 8) & 0x7Fu;
                if (cc == 0) return 10;
                if (cc == 32) return 11;
                return 20;
            }
            if (type == 0xC0u) return 30;
            if (type == 0xE0u) return 40;
            if (type == 0xD0u) return 45;
            if (type == 0x80u) return 50;
            if (type == 0x90u) {
                const unsigned vel = (msg >> 16) & 0x7Fu;
                return vel ? 60 : 50;
            }
            return 70;
        }
        inline bool operator<(const midi_message& a, const midi_message& b)
        {
            if (a.time < b.time) return true;
            if (a.time > b.time) return false;
            const int ra = midi_setup_rank(a.message);
            const int rb = midi_setup_rank(b.message);
            const int ga = ra < 50 ? 0 : 1;
            const int gb = rb < 50 ? 0 : 1;
            if (ga != gb) return ga < gb;
            if (ga == 0 && ra != rb) return ra < rb;
            return false;
        }
        uint_least32_t read_variable_value(void* fp, int(*fgetc)(void*), uint_least32_t* track_length, const char* errtext)
        {
            int ret = 0;
            int d;
            do{
                --*track_length;
                d = fgetc(fp);
                if(d == EOF){
                    throw load_error(errtext);
                }
                ret <<= 7;
                ret |= (d & 0x7F);
            }while(d & 0x80);
            return ret;
        }
    //}//í by Kobarin

    sequencer::sequencer():
        position(messages.begin())
    {
    }
    void sequencer::clear()
    {
        messages.clear();
        long_messages.clear();
        position = messages.begin();
    }
    bool sequencer::load(void* fp, int(*fgetc)(void*))
    {
        bool result = false;
        try{
            clear();
            int b0 = fgetc(fp);
            int b1 = fgetc(fp);
            int b2 = fgetc(fp);
            int b3 = fgetc(fp);
            if(b0 == 0x4D && b1 == 0x54 && b2 == 0x68 && b3 == 0x64){
                load_smf(fp, fgetc);
                result = true;
            }else{
                throw load_error("unsupported format");
            }
        }catch(load_error&){
        }
        if(!result){
            clear();
        }
        position = messages.begin();
        return result;
    }
    static int fpfgetc(void* fp)
    {
        return getc(static_cast<std::FILE*>(fp));
    }
    bool sequencer::load(std::FILE* fp)
    {
        return load(fp, fpfgetc);
    }
    int sequencer::get_num_ports()const
    {
        int ret = 0;
        for(std::vector<midi_message>::const_iterator i = messages.begin(); i != messages.end(); ++i){
            if(ret < i->port){
                ret = i->port;
            }
        }
        return ret + 1;
    }
    double sequencer::get_total_time()const
    {
        if(messages.empty()){
            return 0;
        }else{
            return messages.back().time;
        }
    }
    std::string sequencer::get_title()const
    {
        for(std::vector<midi_message>::const_iterator i = messages.begin(); i != messages.end(); ++i){
            if(i->track == 0 && (i->message & 0xFF) == 0xFF){
                assert((i->message >> 8) < long_messages.size());
                const std::string& s = long_messages[i->message >> 8];
                if(s.size() > 1 && s[0] == 0x03){
                    return s.substr(1);
                }
            }
        }
        return std::string();
    }
    std::string sequencer::get_copyright()const
    {
        for(std::vector<midi_message>::const_iterator i = messages.begin(); i != messages.end(); ++i){
            if(i->track == 0 && (i->message & 0xFF) == 0xFF){
                assert((i->message >> 8) < long_messages.size());
                const std::string& s = long_messages[i->message >> 8];
                if(s.size() > 1 && s[0] == 0x02){
                    return s.substr(1);
                }
            }
        }
        return std::string();
    }
    std::string sequencer::get_song()const
    {
        std::string ret;
        for(std::vector<midi_message>::const_iterator i = messages.begin(); i != messages.end(); ++i){
            if(i->track == 0 && (i->message & 0xFF) == 0xFF){
                assert((i->message >> 8) < long_messages.size());
                const std::string& s = long_messages[i->message >> 8];
                assert(s.size() >= 1);
                if(s[0] == 0x05){
                    ret += s.substr(1);
                }
            }
        }
        return ret;
    }
    void sequencer::play(double time, output* out)
    {
        if(position != messages.begin() && (position - 1)->time >= time){
            position = messages.begin();
        }
        if(position == messages.begin() && position != messages.end() && position->time < time){
            out->reset();
        }
        while(position != messages.end() && position->time < time){
            uint_least32_t message = position->message;
            int port = position->port;
            ++position;
            switch(message & 0xFF){
            case 0xF0:
                {
                    assert((message >> 8) < long_messages.size());
                    const std::string& s = long_messages[static_cast<int>(message >> 8)];
                    out->sysex_message(port, s.data(), s.size());
                }
                break;
            case 0xFF:
                {
                    assert((message >> 8) < long_messages.size());
                    const std::string& s = long_messages[static_cast<int>(message >> 8)];
                    assert(s.size() >= 1);
                    out->meta_event(static_cast<unsigned char>(s[0]), s.data() + 1, s.size() - 1);
                }
                break;
            default:
                out->midi_message(port, message);
                break;
            }
        }
    }
    double sequencer::peek_time() const
    {
        if(position == messages.end()) return 1.0e300;
        return position->time;
    }
    void sequencer::play_forward(double time, output* out)
    {
        while(position != messages.end() && position->time < time){
            uint_least32_t message = position->message;
            int port = position->port;
            ++position;
            switch(message & 0xFF){
            case 0xF0:
                {
                    assert((message >> 8) < long_messages.size());
                    const std::string& s = long_messages[static_cast<int>(message >> 8)];
                    out->sysex_message(port, s.data(), s.size());
                }
                break;
            case 0xFF:
                {
                    assert((message >> 8) < long_messages.size());
                    const std::string& s = long_messages[static_cast<int>(message >> 8)];
                    assert(s.size() >= 1);
                    out->meta_event(static_cast<unsigned char>(s[0]), s.data() + 1, s.size() - 1);
                }
                break;
            default:
                out->midi_message(port, message);
                break;
            }
        }
    }
    void sequencer::set_position(double time)
    {
        position = messages.begin();
        while(position != messages.end() && position->time < time)
            ++position;
    }
    double sequencer::find_marker(const char* name) const
    {
        if(!name || !name[0]) return -1.0;
        const size_t n = strlen(name);
        for(auto pos = messages.begin(); pos != messages.end(); ++pos){
            if((pos->message & 0xFF) != 0xFF) continue;
            const int idx = static_cast<int>(pos->message >> 8);
            if(idx < 0 || idx >= static_cast<int>(long_messages.size())) continue;
            const std::string& s = long_messages[idx];
            if(s.size() != 1 + n) continue;
            if(static_cast<unsigned char>(s[0]) != 0x06) continue;
            if(memcmp(s.data() + 1, name, n) == 0)
                return pos->time;
        }
        return -1.0;
    }
    void sequencer::load_smf(void* fp, int(*fgetc)(void*))
    {
        int hl0 = fgetc(fp);
        int hl1 = fgetc(fp);
        int hl2 = fgetc(fp);
        int hl3 = fgetc(fp);
        if(hl0 == EOF || hl1 == EOF || hl2 == EOF || hl3 == EOF){
            throw load_error("invalid file header");
        }
        unsigned header_len = (static_cast<unsigned>(hl0) << 24)
                            | (static_cast<unsigned>(hl1) << 16)
                            | (static_cast<unsigned>(hl2) << 8)
                            | static_cast<unsigned>(hl3);
        if(header_len < 6){
            throw load_error("invalid file header");
        }
        int format = (fgetc(fp) << 8) | fgetc(fp);
        if(format != 0 && format != 1 && format != 2){
            throw load_error("unsupported format type");
        }
        header_len -= 2;
        int t0 = fgetc(fp);
        int t1 = fgetc(fp);
        unsigned num_tracks = (t0 << 8) | t1;
        int d0 = fgetc(fp);
        int d1 = fgetc(fp);
        unsigned division = (d0 << 8) | d1;
        header_len -= 4;
        while(header_len > 0){
            if(fgetc(fp) == EOF){
                throw load_error("invalid file header");
            }
            --header_len;
        }
        for(unsigned track = 0; track < num_tracks; ++track){
            if(fgetc(fp) != 0x4D || fgetc(fp) != 0x54 || fgetc(fp) != 0x72 || fgetc(fp) != 0x6B){
                throw load_error("invalid track header");
            }
            int l0 = fgetc(fp);
            int l1 = fgetc(fp);
            int l2 = fgetc(fp);
            int l3 = fgetc(fp);
            uint_least32_t track_length = (static_cast<uint_least32_t>(l0) << 24)
                                        | (static_cast<uint_least32_t>(l1) << 16)
                                        | (l2 << 8)
                                        | l3;
            int running_status = 0;
            double time_offset = 0;
            uint_least32_t time = 0;
            midi_message msg;
            msg.port = 0;
            msg.track = track;
            /* F0 が F7 で終わらないときは、続く F7 パケットを足して 1 本の SysEx にする。 */
            std::string sxOpen;
            for(;;){
                if(track_length < 4){
                    throw load_error("unexpected EOF (track_length)");
                }
                uint_least32_t delta = read_variable_value(fp, fgetc, &track_length, "unexpected EOF (deltatime)");
                time += delta;
                if(division & 0x8000){
                    int fps = ~(division >> 8) + 1;
                    int frames = division & 0xFF;
                    msg.time = time / (frames * fps) + time_offset;
                }else{
                    msg.time = time;
                }
                int param = fgetc(fp);
                --track_length;
                switch(param){
                case 0xF0:
                case 0xF7:
                    {
                        int n = read_variable_value(fp, fgetc, &track_length, "unexpected EOF (sysex length)");
                        std::string chunk;
                        if(param == 0xF0){
                            chunk.assign(static_cast<size_t>(n) + 1, '\0');
                            chunk[0] = static_cast<char>(0xF0);
                            for(int i = 1; i <= n; ++i){
                                chunk[i] = static_cast<char>(fgetc(fp));
                            }
                        }else{
                            chunk.assign(static_cast<size_t>(n), '\0');
                            for(int i = 0; i < n; ++i){
                                chunk[i] = static_cast<char>(fgetc(fp));
                            }
                        }
                        track_length -= n;
                        if(param == 0xF7 && !sxOpen.empty())
                            sxOpen.append(chunk);
                        else
                            sxOpen.swap(chunk);
                        const bool done = !sxOpen.empty()
                            && static_cast<unsigned char>(sxOpen[sxOpen.size() - 1]) == 0xF7;
                        if(done){
                            if(static_cast<unsigned char>(sxOpen[0]) != 0xF0)
                                sxOpen.insert(sxOpen.begin(), static_cast<char>(0xF0));
                            msg.message = 0xF0 | (long_messages.size() << 8);
                            messages.push_back(msg);
                            long_messages.push_back(sxOpen);
                            sxOpen.clear();
                        }
                        running_status = 0;
                    }
                    break;
                case 0xF1:
                case 0xF3:
                    {
                        int d = fgetc(fp);
                        --track_length;
                        msg.message = static_cast<uint_least32_t>(param) | (static_cast<uint_least32_t>(d & 0x7F) << 8);
                        messages.push_back(msg);
                        running_status = 0;
                    }
                    break;
                case 0xF2:
                    {
                        int d0 = fgetc(fp);
                        int d1 = fgetc(fp);
                        track_length -= 2;
                        msg.message = static_cast<uint_least32_t>(param)
                            | (static_cast<uint_least32_t>(d0 & 0x7F) << 8)
                            | (static_cast<uint_least32_t>(d1 & 0x7F) << 16);
                        messages.push_back(msg);
                        running_status = 0;
                    }
                    break;
                case 0xF4:
                case 0xF5:
                case 0xF6:
                case 0xF8:
                case 0xF9:
                case 0xFA:
                case 0xFB:
                case 0xFC:
                case 0xFD:
                case 0xFE:
                    msg.message = static_cast<uint_least32_t>(param);
                    messages.push_back(msg);
                    if(param < 0xF8)
                        running_status = 0;
                    break;
                case 0xFF:
                    {
                        int type = fgetc(fp);
                        --track_length;
                        int n = read_variable_value(fp, fgetc, &track_length, "unexpected EOF (metaevent)");
                        std::string s(n + 1, '\0');
                        s[0] = static_cast<char>(type);
                        for(int i = 1; i <= n; ++i){
                            s[i] = static_cast<char>(fgetc(fp));
                        }
                        track_length -= n;
                        msg.message = 0xFF | (long_messages.size() << 8);
                        messages.push_back(msg);
                        long_messages.push_back(s);
                        switch(type){
                        case 0x21:
                            if(n == 1){
                                msg.port = static_cast<unsigned char>(s[1]);
                            }
                            break;
                        case 0x2F:
                            goto next_track;
                        case 0x54:
                            if(n != 5){
                                throw load_error("invalid SMTPE offset metaevent length");
                            }
                            if(msg.time == 0 && (division & 0x8000)){
                                int hour = static_cast<unsigned char>(s[1]);
                                int min = static_cast<unsigned char>(s[2]);
                                int sec = static_cast<unsigned char>(s[3]);
                                int frame = static_cast<unsigned char>(s[4]);
                                int subframe = static_cast<unsigned char>(s[5]);
                                double fps;
                                switch(hour >> 5){
                                case 0:
                                    fps = 24;
                                    break;
                                case 1:
                                    fps = 25;
                                    break;
                                case 2:
                                    fps = 29.97;
                                    break;
                                case 3:
                                    fps = 30;
                                    break;
                                }
                                time = 0;
                                time_offset = (hour & 0x1F) * 3600 + min * 60 + sec + (frame + subframe / 100.0) * fps;
                            }
                            break;
                        }
                    }
                    break;
                default:
                    if(param & 0x80){
                        running_status = param;
                        param = fgetc(fp);
                        --track_length;
                    }
                    switch(running_status & 0xF0){
                    case 0xC0:
                    case 0xD0:
                        msg.message = running_status | (param << 8);
                        break;
                    case 0x80:
                    case 0x90:
                    case 0xA0:
                    case 0xB0:
                    case 0xE0:
                        msg.message = running_status | (param << 8) | (fgetc(fp) << 16);
                        --track_length;
                        break;
                    default:
                        /* チャンネルメッセージでないバイトはここで消費済み。トラックは続ける。 */
                        running_status = 0;
                        continue;
                    }
                    messages.push_back(msg);
                    break;
                }
            }
        next_track:
            if(!sxOpen.empty()){
                if(static_cast<unsigned char>(sxOpen[0]) != 0xF0)
                    sxOpen.insert(sxOpen.begin(), static_cast<char>(0xF0));
                if(sxOpen.empty() || static_cast<unsigned char>(sxOpen[sxOpen.size() - 1]) != 0xF7)
                    sxOpen.push_back(static_cast<char>(0xF7));
                msg.message = 0xF0 | (long_messages.size() << 8);
                messages.push_back(msg);
                long_messages.push_back(sxOpen);
                sxOpen.clear();
            }
            if(track_length < 0){
                throw load_error("unexpected EOF (over track_length)");
            }
            while(track_length > 0){
                if(fgetc(fp) == EOF){
                    throw load_error("unexpected EOF (tailer padding)");
                }
                --track_length;
            }
        }
        std::stable_sort(messages.begin(), messages.end());
        if(!(division & 0x8000)){
            uint_least32_t tempo = 500000;
            double time_offset = 0;
            double base = 0;
            for(std::vector<midi_message>::iterator i = messages.begin(); i != messages.end(); ++i){
                double org_time = i->time;            
                i->time = (i->time - base) * tempo / 1000000.0 / division + time_offset;
                if((i->message & 0xFF) == 0xFF){
                    assert((i->message >> 8) < long_messages.size());
                    const std::string& s = long_messages[i->message >> 8];
                    if(s.size() == 4 && s[0] == 0x51){
                        tempo = (static_cast<uint_least32_t>(static_cast<unsigned char>(s[1])) << 16)
                              | (static_cast<unsigned char>(s[2]) << 8)
                              | static_cast<unsigned char>(s[3]);
                        base = org_time;
                        time_offset = i->time;
                    }
                }
            }
        }
    }
    double sequencer::get_loop_point(void)//add By Kobarin
    {//[v|Cgª êÎ»ÌðÔ·
     //È¢êÍ -1.0 ðÔ·
         for(auto pos = messages.begin() ; pos != messages.end(); pos++){
            uint_least32_t message = pos->message;
            if((message&0xFFF0) == 0x6FB0){//[v|Cg(CC111)
                return pos->time;
            }
         }
         return -1.0;
    }

//}//í by Kobarin
