#ifndef midisequencer_hpp
#define midisequencer_hpp

#include <cstdio>
#include <stdint.h>
#include <string>
#include <vector>
//namespace midisequencer{//í by Kobarin
//    typedef unsigned long uint_least32_t;

    struct midi_message{
        double time;
        uint_least32_t message;
        int port;
        int track;
    };
/* í by Kobarin
    class uncopyable{
    public:
        uncopyable(){}
    private:
        uncopyable(const uncopyable&);
        void operator=(const uncopyable&);
    };
*/
    class output{//:uncopyable{//C³ by Kobarin
    public:
        virtual void midi_message(int port, uint_least32_t message) = 0;
        virtual void sysex_message(int port, const void* data, std::size_t size) = 0;
        virtual void meta_event(int type, const void* data, std::size_t size) = 0;
        virtual void reset() = 0;
    protected:
        ~output(){}
    };

    class sequencer{//:uncopyable{//C³ by Kobarin
    public:
        sequencer();
        void clear();
        bool load(void* fp, int(*fgetc)(void*));
        bool load(std::FILE* fp);
        int get_num_ports()const;
        double get_total_time()const;
        std::string get_title()const;
        std::string get_copyright()const;
        std::string get_song()const;
        void play(double time, output* out);
        void play_forward(double time, output* out);
        void set_position(double time);
        bool is_play_end(void){return position >= messages.end();}//ÇÁ by Kobarin
        double get_loop_point(void);//ÇÁ by Kobarin
        double find_marker(const char* name) const;
    private:
        std::vector<midi_message> messages;
        std::vector<midi_message>::iterator position;
        std::vector<std::string> long_messages;
        void load_smf(void* fp, int(*fgetc)(void*));
    };
//}//í by Kobarin

#endif
