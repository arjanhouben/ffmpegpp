#include "ffmpeg++.h"

#include <iostream>
#include <fstream>
#include <vector>

using namespace std;

void write_ppm( const string &output, size_t stride, size_t height, const char *img )
{
	ofstream ppm( output, ios::binary );
	ppm << "P6\n" << stride << ' ' << height << "\n255\n";
	for ( auto y = 0; y < height; ++y )
	{
		for ( auto x = 0; x < stride; ++x )
		{
			ppm.write( img, 1 );
			ppm.write( img, 1 );
			ppm.write( img, 1 );
			++img;
		}
		ppm << '\n';
	}
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
	
	auto f = av::format::open_input( std::move( fc ), "", inf );
	
	auto henk = [output]( AVFrame &frame )
	{
		auto picture = reinterpret_cast< AVPicture* >( &frame );
		auto img = reinterpret_cast< const char* >( picture->data[ 0 ] );
		
		write_ppm( output, picture->linesize[ 0 ], frame.height, img );
	};
	
	for ( auto &s : f.streams() )
	{
		s.open( henk );
	}
	
	auto frame = av::frame::alloc();
	
	auto pkt = av::packet();
	while ( f.decode( pkt, frame ) )
	{
		//
	}
}

void test_file_read( const string &input, const string &output )
{
	auto context = av::format::alloc_context();
	auto format = av_find_input_format( "mjpeg" ) || av::error( "could not find mjpeg format" );

	auto f = av::format::open_input( std::move( context ), input.c_str(), format );
	
	auto callback = [output]( AVFrame &frame )
	{
		auto picture = reinterpret_cast< AVPicture* >( &frame );
		auto img = reinterpret_cast< const char* >( picture->data[ 0 ] );
		
		write_ppm( output, picture->linesize[ 0 ], frame.height, img );
	};

	for( auto &s : f.streams() )
	{
		s.open( callback );
	}
	
	av::packet pkt;
	auto frame = av::frame::alloc();
	while ( f.decode( pkt, frame ) )
	{
		//
	}
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
