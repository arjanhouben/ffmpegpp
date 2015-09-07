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
	
	uint8_t tmp[ 64 ];
	ioctx->buffer = tmp;
	ioctx->buf_end = tmp + sizeof( tmp );
	ioctx->buf_ptr = tmp;
	ioctx->buffer_size = sizeof( tmp );

	auto bufstart = reinterpret_cast< uint8_t* >( data.data() );
	auto bufend = bufstart + data.size();
	
	ioctx.read = [bufstart,bufend]( uint8_t *buffer, int size ) mutable
	{
		auto count = min< int >( bufend - bufstart, size );
		copy( bufstart, bufstart + count, buffer );
		bufstart += count;
		return count;
	};

	auto f = av::format::open_input( "", av::format::make_context( ioctx ), inf );
	
	vector< char > buffer;
	
	auto henk = [output,&buffer]( AVFrame &frame )
	{
		buffer.resize( 3 * frame.width * frame.height );
		
		sws::convert( frame, buffer.data(), frame.width, AV_PIX_FMT_RGB24 );
		
		write_ppm( output, frame.width, frame.height, buffer.data() );
		
		return true;
	};
	
	for ( auto &s : f.streams() )
	{
		s.open_input( henk );
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
		
		return true;
	};

	for( auto &s : f.streams() )
	{
		s.open_input( callback );
	}
	
	f.decode_all();
}


void sin_to_mp3( const string &input, const string &output )
{
	auto f = av::format::open_output( output.c_str() );
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
  
		return true;
	};
	
	stream.open_output( henk );
	
	av::packet p;
	auto frame = av::frame::alloc();
	f.encode( p, frame );
}


void test_file_write( const string &output )
{
	auto file = av::format::open_output( output.c_str() );
	
	auto video = file.add_stream( AV_CODEC_ID_MJPEG );
	
	const auto width = 320, height = 240, bpp = 2;
	
	vector< uint8_t > data( width * height * bpp );
	std::fill( data.begin(), data.end(), 0xAA );
	
	vector< uint8_t > convertbuffer( width * height * bpp );
	
	
	video->codec->pix_fmt = AV_PIX_FMT_YUVJ422P;
	video->codec->width = width;
	video->codec->height = height;
	video->codec->gop_size = 12;
	video->codec->qmax = 5;
	video->codec->qmin = 2;
	video->codec->time_base.num = 1;
	video->codec->time_base.den = 25;
	video->codec->bit_rate = 4000000;
	
	auto henk = [&]( AVFrame &dstframe )
	{
		sws::helper src, dst;
		src.data[ 0 ] = data.data();
		src.stride[ 0 ] = width * bpp;
		src.format = video->codec->pix_fmt;
		src.width = width;
		src.height = height;

		dst.stride[ 0 ] = width;
		dst.stride[ 1 ] = width / 2;
		dst.stride[ 2 ] = width / 2;
		dst.data[ 0 ] = &convertbuffer[ 0 ];
		dst.data[ 1 ] = &convertbuffer[ 0 ] + dst.stride[ 0 ];
		dst.data[ 2 ] = &convertbuffer[ 1 ] + dst.stride[ 1 ];
		dst.format = AV_PIX_FMT_YUV422P;
		dst.width = width;
		dst.height = height;
		
		sws::convert( src, dst );
		
		dst.to_avframe( dstframe );
		
		return true;

//		sws::pointers_t pointers( dstframe.data, dstframe.data + AV_NUM_DATA_POINTERS );
//		sws::strides_t strides( dstframe.linesize, dstframe.linesize + AV_NUM_DATA_POINTERS );
//		
//		sws::convert( tmpframe, pointers, strides, AV_PIX_FMT_YUV422P );
	};
	
	video.open_output( henk );
	
	av::packet p;
	auto frame = av::frame::alloc();
	file.encode( p, frame );
}

int main( int argc, char **argv )
{
	try
	{
		av_register_all();
	
//		test_manual_file_read( "test.jpg", "out.ppm" );
//		test_file_read( "test.jpg", "out2.ppm" );
//		sin_to_mp3( "test.wav", "out.mp3" );
		test_file_write( "out.mjpeg" );
	}
	catch( const exception &err )
	{
		cerr << err.what() << endl;
		return 1;
	}
	
	return 0;
}
