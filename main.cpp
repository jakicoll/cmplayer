#include <boost/program_options.hpp>
namespace po = boost::program_options;

#include <iostream>
#include <deque>
#include <array>
#include <cstdint>
#include <math.h> //fabs
#include <memory.h> //evil memset.

#include <jack/jack.h>
#include <pstreams/pstream.h>
#include <sys/time.h>

#include <unistd.h>

#define CHANNELS 2
#define EXIT_SECONDS_AFTER_SONG 5

using namespace std;

typedef int16_t sample;

//Static stuff, that could be avoided with OOP. (fixme)
jack_port_t *output_port_1;
jack_port_t *output_port_2;
jack_client_t *client;
std::deque<sample> dq;

//Programm Options (boost)
string songpath;
string jack_portname;
string importscript;
bool autoconnect;

//Runtime variables

/*
 * If it is not explicitely declared otherwise, sample always refers to the total sample for both channels;
 * a STEREO track of 1 second with 44100 samplerate has 88200 samples.
 */
long player_sample_position=0;
struct timeval tval_start;
int log_count=0;
double target_seconds;
double song_duration_seconds=-1;
jack_nframes_t samplerate;

double skip_threshold;

/* Credits go out to Explicit C++
<http://cpp.indi.frih.net/blog/2014/09/how-to-read-an-entire-file-into-memory-in-cpp/> */
//Now there is one really strange thing: Reading a stream of shorts (16 bit), which is out target
//format (as we have 16 bit samples): It does not work. Only a few characters are read and then it's EOF.
//Now matter how hard I tried. It took me more than a day until I decided to read 8 bit, convert on my own and save.
std::deque<sample> read_samples_into_memory(std::basic_istream<char>& in)
{
    using std::begin;
    using std::end;

    auto const chunk_size = 512*512;

    std::deque<sample> container = std::deque<sample>();

    auto chunk = std::array<char, chunk_size>{};

    int chunkcount=0;

    auto converted = std::array<sample, chunk_size*sizeof(char)/sizeof(sample)>{};

    while (in.read(chunk.data(), chunk.size()) ||
      in.gcount())
    {
        //Read one chunk of data, convert it and store it into the container

        //It can be assumed, that in.gcount() is even. :)
        unsigned sample_read_count=in.gcount()/sizeof(sample);
        union {
            int8_t charin[2];
            int16_t sampleout;
        };
        auto chunk_iterator=chunk.begin();
        auto sample_storage_iterator=converted.begin();
        for(unsigned i=0; i<sample_read_count; ++i) {
            charin[0]=*chunk_iterator;
            ++chunk_iterator;
            charin[1]=*chunk_iterator;
            ++chunk_iterator;
            *sample_storage_iterator=sampleout;
            ++sample_storage_iterator;
        }
        container.insert(end(container),
                         begin(converted),
                         begin(converted) + sample_read_count);
        cout << "read " << sample_read_count << " samples. chunk " << ++chunkcount << "." << '\r';
    }
    cout << endl;
    return container;
}

/**
 * Adjusts the pitch and jumps within the track if the offset exceeds a threshold.
 */
void retarget(bool log) {
    //target: seconds in the track; multiply by samplerate to get sample we should be now.
    struct timeval tval_now;
    gettimeofday(&tval_now, NULL);


    //Calculate the position where the player should be right now
    target_seconds=tval_now.tv_sec - tval_start.tv_sec + 0.000001*(tval_now.tv_usec - tval_start.tv_usec);

    //Calculate time difference (independend of sample rate)
    double position_seconds=1.0*player_sample_position/CHANNELS/samplerate;
    double difference_seconds=position_seconds-target_seconds; //positive diff: player is ahead of time.

    if(log)
    cout << "Position: " << position_seconds << " | Target: " << target_seconds << " | DELTA " << difference_seconds << endl;

    if(fabs(difference_seconds) > skip_threshold) {
        player_sample_position=target_seconds*samplerate*CHANNELS;
        cout << "Skipped " << difference_seconds << "seconds." << endl;
    }
}


/**
 * TODO This needs documentation;
 */
int
process (jack_nframes_t nframes, void *arg)
{
    bool log= (log_count == 0);
    log_count= (log_count+1)%10;

    retarget(log); //Could be reduced to every nTh process-call.

    jack_default_audio_sample_t *out1;
    jack_default_audio_sample_t *out2;

    out1 = (jack_default_audio_sample_t*) jack_port_get_buffer (output_port_1, nframes);
    out2 = (jack_default_audio_sample_t*) jack_port_get_buffer (output_port_2, nframes);

    unsigned samples_needed=nframes*CHANNELS;

    unsigned samples_in_memory=dq.size();
    unsigned last_sample_needed=player_sample_position+samples_needed;

    bool whole_frame_is_pre_playback = last_sample_needed < 0; //first sample of the song
    bool enough_sample_in_memory = samples_in_memory >= last_sample_needed;

    if(whole_frame_is_pre_playback || !enough_sample_in_memory) {
        //output silence
        memset(out1, '\0', sizeof(*out1)*nframes);
        memset(out2, '\0', sizeof(*out2)*nframes);

        if(target_seconds > song_duration_seconds + EXIT_SECONDS_AFTER_SONG) {
            cout << "Ich habe fertig. (Was erlauben Strunz?)" << endl;
            //jack_client_close (client);
            //TODO It does not work: The program does not close.
            //Maybe we can not call jack_client_close from within process?
            exit (0);
        }
        //Forward pointer manually
        player_sample_position+=CHANNELS*nframes; //2 Channels
    } else {
        //Each RAWPCM Sample is stored as 16bit (short); We have a stereo rawpcm
        //so it's L1 R1 L2 R2...; Jack however requires us to use floats
        //Short is from -32768 to 32767, so we got to divide by 32768 (which is 2^15)
        float magic=32768.0;
        while(player_sample_position < last_sample_needed) {
            if(player_sample_position < 0) {
                *out1=0;
                *out2=0;
            } else {
                *out1= dq.at(player_sample_position)*1.0/magic;
                *out2= dq.at(player_sample_position+1)*1.0/magic;
            }
            ++out1;
            ++out2;
            player_sample_position+=2;
        }
    }
    return 0;
}


/**
 * JACK calls this shutdown_callback if the server ever shuts down or
 * decides to disconnect the client.
 */
void
jack_shutdown (void *arg)
{
    cout << "Fatal: Jack shutdown triggered" << endl;
    exit (1);
}

int main(const int argc, const char* argv[])
{
    long starttime;
    try {
        po::options_description desc("Allowed options");
        desc.add_options()
                ("help,h", "produce help message and exit")
                ("starttime,t", po::value(&starttime)->default_value(-1), "unix timestamp for track synchronisation; playback starts immediately if ommited.")
                ("song,s", po::value(&songpath)->required(), "path to the song to play")
                ("jackport,j", po::value(&jack_portname), "jack port name")
                ("autoconnect,a", po::bool_switch(&autoconnect)->default_value(true), "cmplayer will automatically connect to speakers")
                ("importer,i", po::value(&importscript)->default_value("./import"), "helperscript to read audio from; relative to working dir (start with ./) or full path.")
                ("skipthreshold", po::value(&skip_threshold)->default_value(0.05), "Tolerance in seconds until audio is skipped to sync");


        po::variables_map vm;
        po::store(po::parse_command_line(argc, argv, desc), vm);

        if (vm.count("help")) {
            cout << desc << "\n";
            return 0;
        }

        po::notify(vm); //Ensures all required options are given.
    } catch(exception& e) {
        cerr << "error: " << e.what() << "\n";
        return 1;
    }
    catch(...) {
        cerr << "Exception of unknown type!\n";
    }

    if(starttime == -1) {
        gettimeofday(&tval_start, NULL);
    } else {
        tval_start.tv_sec=starttime;
        tval_start.tv_usec=0;
    }

    const char **ports;
    const char *client_name = "cmwax";
    const char *server_name = NULL;
    jack_options_t options = JackNullOption;
    jack_status_t status;

    /* open a client connection to the JACK server */

    client = jack_client_open (client_name, options, &status, server_name);
    if (client == NULL)
    {
        fprintf (stderr, "jack_client_open() failed, "
                 "status = 0x%2.0x\n", status);
        if (status & JackServerFailed)
        {
            fprintf (stderr, "Unable to connect to JACK server\n");
        }
        exit (1);
    }
    if (status & JackServerStarted)
    {
        fprintf (stderr, "JACK server started\n");
    }
    if (status & JackNameNotUnique)
    {
        client_name = jack_get_client_name(client);
        fprintf (stderr, "unique name `%s' assigned\n", client_name);
    }

    samplerate=jack_get_sample_rate(client);
    cout << "engine sample rate: "<< samplerate << endl;

    /* tell the JACK server to call `process()' whenever
       there is work to be done.
    */

    jack_set_process_callback (client, process, 0);

    /* tell the JACK server to call `jack_shutdown()' if
       it ever shuts down, either entirely, or if it
       just decides to stop calling us.
    */

    jack_on_shutdown (client, jack_shutdown, 0);

    //const pstreams::pmode mode = pstreams::pstdout|pstreams::pstderr;

    std::vector<std::string> importerargs;

    importerargs.push_back(importscript);
    importerargs.push_back(songpath);
    importerargs.push_back(std::to_string(samplerate));


    redi::ipstream importer(importscript, importerargs); //TODO Error handling!


    dq = read_samples_into_memory(importer);

    song_duration_seconds=dq.size()/samplerate/CHANNELS; //c

    cout << "Read " << dq.size() << " samples; song duration " << song_duration_seconds << " seconds."<< endl;

    /* create two ports */

    output_port_1 = jack_port_register (client, "output_1",
                                      JACK_DEFAULT_AUDIO_TYPE,
                                      JackPortIsOutput, 0);
    output_port_2 = jack_port_register (client, "output_2",
                                      JACK_DEFAULT_AUDIO_TYPE,
                                      JackPortIsOutput, 0);

    if (output_port_1 == NULL || output_port_2==NULL)
    {
        fprintf(stderr, "no more JACK ports available\n");
        exit (1);
    }

    /* Tell the JACK server that we are ready to roll.  Our
     * process() callback will start running now. */

    if (jack_activate (client))
    {
        fprintf (stderr, "cannot activate client");
        exit (1);
    }


    if(autoconnect) {
        /* Connect the ports.  You can't do this before the client is
         * activated, because we can't make connections to clients
         * that aren't running.  Note the confusing (but necessary)
         * orientation of the driver backend ports: playback ports are
         * "input" to the backend, and capture ports are "output" from
         * it.
         */
        ports = jack_get_ports (client, NULL, NULL,
                                JackPortIsPhysical|JackPortIsInput);
        if (ports == NULL)
        {
            fprintf(stderr, "no physical playback ports\n");
            exit (1);
        }
        if (jack_connect (client, jack_port_name (output_port_1), ports[0]))
        {
            fprintf (stderr, "cannot connect output port 1\n");
        }
        if (jack_connect (client, jack_port_name (output_port_2), ports[1]))
        {
            fprintf (stderr, "cannot connect output port 2\n");
        }

        free (ports);
    }

    /* keep running; programm exits in retarget() (called by process()) as soon as the song is over. */

    sleep (-1);
    return 0;
}
