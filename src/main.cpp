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

	auto fc = av::format::alloc_context( ioctx );
	
	auto f = av::format::open_input( "", std::move( fc ), inf );
	
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
		
		sws::convert( frame, { a,b,c }, { frame.width, frame.width, frame.width }, AV_PIX_FMT_YUV444P );
		
		write_ppm( output, frame.width, frame.height, henk.data() );
	};

	for( auto &s : f.streams() )
	{
		s.open( callback );
	}
	
	f.decode_all();
}

int main( int argc, char **argv )
{
	try
	{
		av_register_all();
	
		test_manual_file_read( "test.jpg", "out.ppm" );
		test_file_read( "test.jpg", "out2.ppm" );
	}
	catch( const exception &err )
	{
		cerr << err.what() << endl;
		return 1;
	}
	
	return 0;
}
