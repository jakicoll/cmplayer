#include <boost/program_options.hpp>
namespace po = boost::program_options;

#include <iterator>
#include <iostream>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
using namespace std;

int main (const int argc, const char* argv[])
{
    try {
        string songpath="asb";
        string jack_portname;
        long starttime=123;
        bool autoconnect=false;
        po::options_description desc("Allowed options");
        desc.add_options()
                ("help", "produce help message and exit")
                ("starttime", po::value(&starttime)/*->required()*/, "unix timestamp for track synchronisation")
                ("song", po::value(&songpath)/*->required()*/, "path to the song to play")
                ("jackport", po::value(&jack_portname), "jack port name")
                ("speakers", po::value<string>(), "cmplayer connects its own output to the given jack input")
                ("autoconnect", po::bool_switch(&autoconnect), "cmplayer will automatically connect to speakers");

        po::variables_map vm;
        po::store(po::parse_command_line(argc, argv, desc), vm);
        po::notify(vm);

        if (vm.count("help")) {
            cout << desc << "\n";
            return 0;
        }


    } catch(exception& e) {
        cerr << "error: " << e.what() << "\n";
        return 1;
    }
    catch(...) {
        cerr << "Exception of unknown type!\n";
    }
    return 0;

}

