#include "ffmpeg++.h"

#include <iostream>
#include <fstream>
#include <vector>

using namespace std;

void write_ppm( const string &output, size_t stride, size_t height, const void *ptr )
{
	ofstream ppm( output, ios::binary );
	ppm << "P6\n" << stride << ' ' << height << "\n255\n";
	ppm.write( reinterpret_cast< const char* >( ptr ), stride * height * 3 );
}

void test_manual_file_read( const string &input, const string &output )
{
	ifstream file( input, ios::binary );
	file.seekg( 0, ios::end );
	auto size = file.tellg();
	file.seekg( 0, ios::beg );
	
	vector< unsigned char > data( size );
	file.read( reinterpret_cast< char* >( data.data() ), size );

	auto inf = av_find_input_format( "mjpeg" ) || av::error( "could not find mjpeg format" );

	auto ioctx = av::io::context::alloc();

	auto bufstart = reinterpret_cast< uint8_t* >( data.data() );
	auto bufend = bufstart + data.size();
	
	ioctx->read = [bufstart,bufend]( uint8_t *buffer, int size ) mutable
	{
		auto count = min< size_t >( bufend - bufstart, size );
		copy( bufstart, bufstart + count, buffer );
		bufstart += count;
		return count;
	};

	auto f = av::format::open_input( "", av::format::context( ioctx ), inf );
	
	vector< char > buffer;
	
	auto henk = [output,&buffer]( AVFrame &frame )
	{
		buffer.resize( 3 * frame.width * frame.height );
		
		sws::convert( frame, buffer.data(), frame.width, AV_PIX_FMT_RGB24 );
		
		write_ppm( output, frame.width, frame.height, buffer.data() );
	};
	
	for ( auto &s : f.streams() )
	{
		s.open( henk );
	}
	
	f.decode_all();
}

void test_file_read( const string &input, const string &output )
{
	auto f = av::format::open_input( input.c_str() );
	
	auto callback = [output]( AVFrame &frame )
	{
		vector< uint8_t > henk( frame.width * frame.height * 3 );
		
		auto a = henk.data();
		auto b = a + frame.width * frame.height;
		auto c = b + frame.width * frame.height;
		
        sws::convert( frame, sws::pointers_t( a,b,c ), sws::strides_t( frame.width, frame.width, frame.width ), AV_PIX_FMT_YUV444P );
		
		write_ppm( output, frame.width, frame.height, henk.data() );
	};

	for( auto &s : f.streams() )
	{
		s.open( callback );
	}
	
	f.decode_all();
}


void sin_to_mp3( const string &input, const string &output )
{
	auto ctx = av::format::context();
	cout << ctx.get() << endl;
	auto f = av::format::open_output( output.c_str(), std::move( ctx ) );
	auto stream = f.add_stream( AV_CODEC_ID_MP2 );
	cout << stream->codec << endl;
	
	stream->codec->bit_rate		 = 64000;
	stream->codec->sample_fmt	 = AV_SAMPLE_FMT_S16;
	stream->codec->sample_rate    = 44100;
	stream->codec->channel_layout = AV_CH_LAYOUT_STEREO;
	stream->codec->channels       = av_get_channel_layout_nb_channels( AV_CH_LAYOUT_STEREO );
	
	vector< int16_t > samples;
	
	float t = 0;

	auto henk = [&]( AVFrame &frame )
	{
		auto c = stream->codec;
		
		samples.resize( c->frame_size );
		for ( auto &s : samples )
		{
			s = sin( t ) * 0x7fff;
			t += 44 / 7 * 440 / c->sample_rate;
		}
		
		frame.nb_samples = c->frame_size;
		frame.format = c->sample_fmt;
		frame.channel_layout = c->channel_layout;
		
		auto buffersize = av_samples_get_buffer_size( nullptr, c->channels, c->frame_size, c->sample_fmt, 0 );
		buffersize < av::error( "could not get buffers size" );
		
		auto ret = avcodec_fill_audio_frame( &frame, c->channels, c->sample_fmt, (const uint8_t*)samples.data(), samples.size(), 0 );
  
	};
	
	stream.open( henk );
	
	av::packet p;
	auto frame = av::frame::alloc();
	f.encode( p, frame );
}

int main( int argc, char **argv )
{
	try
	{
		av_register_all();
	
		test_manual_file_read( "test.jpg", "out.ppm" );
		test_file_read( "test.jpg", "out2.ppm" );
//		sin_to_mp3( "test.wav", "out.mp3" );
	}
	catch( const exception &err )
	{
		cerr << err.what() << endl;
		return 1;
	}
	
	return 0;
}
