#pragma once

#include <string>
#include <functional>
#include <memory>
#include <vector>
#include <array>

extern "C"
{
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"
}

namespace av
{
	struct error
	{
		std::string message;
		
		error( const std::string &m = std::string() ) :
			message( m ) {}
		
		void operator()( int e ) const
		{
			char buffer[ AV_ERROR_MAX_STRING_SIZE ];
			
			av_make_error_string( buffer, AV_ERROR_MAX_STRING_SIZE, e );
			
			auto len = strnlen( buffer, AV_ERROR_MAX_STRING_SIZE );
			buffer[ len ] = 0;
			
			throw std::runtime_error( message + ": " + buffer );
		}
		
		void operator()( const std::string &m ) const
		{
			throw std::runtime_error( message + ": " + m );
		}
	};
	
	template < typename T >
	T operator < ( T &&t, error &&e )
	{
		if ( t < 0 ) e( t );
		return std::forward< T >( t );
	}
	
	template < typename T >
	T* operator || ( T *t, error &&e )
	{
		if ( !t ) e( "received nullptr" );
		return t;
	}
	
	template < typename T, typename D >
	std::unique_ptr< T, D > make_unique( T *t, D d )
	{
		return std::unique_ptr< T, D >( t, d );
	}
	
	typedef decltype( make_unique( av_malloc( 0 ), &av_free ) ) pointer_type;
	
	struct buffer : pointer_type
	{
		const size_t size;
		
		buffer( size_t s ) :
			pointer_type( malloc( s ), &av_free ),
			size( s ) { }
		
		unsigned char* data()
		{
			return reinterpret_cast< unsigned char* >( get() );
		}
		
		const unsigned char* data() const
		{
			return reinterpret_cast< unsigned char* >( get() );
		}
	};

	pointer_type malloc( size_t s )
	{
		return make_unique( av_malloc( s ), &av_free );
	}
	
	namespace io
	{
		namespace context
		{
			class type;
			typedef std::unique_ptr< type > pointer_type;
		}
	}
	
	namespace format
	{
		typedef decltype( make_unique( avformat_alloc_context(), &avformat_free_context ) ) pointer_type;
		pointer_type alloc_context( const io::context::pointer_type &ptr = io::context::pointer_type() );
	}
	
	struct packet : AVPacket
	{
		packet()
		{
			av_init_packet( this );
		}
		packet( const packet& ) = delete;
		packet( packet&& ) = delete;
		~packet()
		{
			av_free_packet( this );
		}
	};
	
	bool read_frame( format::pointer_type &p, packet &pack )
	{
		auto result = av_read_frame( p.get(), &pack );
		if ( result )
		{
			if ( result == AVERROR_EOF )
			{
				return false;
			}
			
			result < error( "could not read frame" );
		}
		
		return true;
	}
	
	namespace frame
	{
		// av_frame_free expects a AVFrame** which does not play nice with unique_ptrs
		void free( AVFrame *f )
		{
			av_frame_free( &f );
		}

		typedef decltype( make_unique( av_frame_alloc(), &free ) ) pointer_type;
		
		pointer_type alloc()
		{
			return make_unique( av_frame_alloc(), &free );
		}
	}
	
	namespace codec
	{
		bool decode_video( AVCodecContext *codec, frame::pointer_type &p, const AVPacket &packet )
		{
			int frameFinished = false;
			avcodec_decode_video2( codec, p.get(), &frameFinished, &packet ) < error( "could not decode video" );
			return frameFinished;
		}
	}
	
	namespace io
	{
		namespace context
		{
			class type;
			typedef std::unique_ptr< type > pointer_type;
			
			class type
			{
				typedef std::unique_ptr< AVIOContext, decltype( &av_free ) > pointer_type;
			
				public:
				
					type( buffer &&b ) :
						read(),
						write(),
						seek(),
						context_( nullptr, &av_free ),
						buffer_( std::move( b ) ) {}
				
					std::function< int( uint8_t*, int ) > read, write;
					std::function< int64_t(int64_t,int) > seek;
				
				private:
				
					inline int read_cb( uint8_t *b, int s )
					{
						return read( b, s );
					}
				
					inline int write_cb( uint8_t *b, int s )
					{
						return write( b, s );
					}
				
					inline int64_t seek_cb( int64_t b, int s )
					{
						return seek( b, s );
					}
				
					pointer_type context_;
					buffer buffer_;
				
					friend std::unique_ptr< type > alloc( buffer &&b );
					friend struct callback;
					friend format::pointer_type format::alloc_context( const io::context::pointer_type &ptr );
			};
			
			struct callback
			{
				static int read( void *p, uint8_t *b, int s )
				{
					return reinterpret_cast< type* >( p )->read_cb( b, s );
				}
				
				static int write( void *p, uint8_t *b, int s )
				{
					return reinterpret_cast< type* >( p )->write_cb( b, s );
				}
				
				static int64_t seek( void *p, int64_t b, int s )
				{
					return reinterpret_cast< type* >( p )->seek_cb( b, s );
				}
			};
			
			std::unique_ptr< type > alloc( buffer &&b )
			{
				auto result = std::unique_ptr< type >( new type( std::move( b ) ) );
				auto ctx = avio_alloc_context( result->buffer_.data(), result->buffer_.size, false, result.get(), &callback::read, &callback::write, &callback::seek );
				result->context_.reset( ctx );
				return result;
			}
			
			std::unique_ptr< type > alloc( size_t s = 4096 )
			{
				return alloc( buffer( s ) );
			}
		}
	}
		
	typedef std::function< void( AVFrame &frame ) > callback_t;
	
	struct stream
	{
		stream( AVStream &s, callback_t &c ) : stream_( &s ), cb_( &c ) {}
		
		explicit operator bool() const
		{
			return bool( *cb_ );
		}
		
		AVStream* get() const
		{
			return stream_;
		}
		
		AVStream* operator ->() const
		{
			return stream_;
		}
		
		void open( const callback_t &cb )
		{
			stream_->discard = AVDISCARD_DEFAULT;
			*cb_ = cb;
			auto codec = stream_->codec;
			auto decoder = avcodec_find_decoder( codec->codec_id );
			avcodec_open2( codec, decoder, nullptr ) < av::error( "could not open codec" );
		}
		
		void close()
		{
			stream_->discard = AVDISCARD_ALL;
			*cb_ = callback_t();
		}
		
		void call( AVFrame &frame )
		{
			(*cb_)( frame );
		}
		
		private:
			AVStream *stream_;
			callback_t *cb_;
	};
	
	bool decode( stream &stream, AVPacket &p, AVFrame &frame )
	{
		int frame_complete = false;
		switch( stream->codec->codec_type )
		{
			case AVMEDIA_TYPE_VIDEO:
			{
				auto result = avcodec_decode_video2( stream->codec, &frame, &frame_complete, &p );
				result < error( "could not decode video" );
				if ( frame_complete )
				{
					stream.call( frame );
				}
				return result;
			}
			case AVMEDIA_TYPE_AUDIO:
			case AVMEDIA_TYPE_SUBTITLE:
			case AVMEDIA_TYPE_UNKNOWN:
			case AVMEDIA_TYPE_DATA:
			case AVMEDIA_TYPE_ATTACHMENT:
			case AVMEDIA_TYPE_NB:
				break;
		}
		return false;
	}
	
	namespace format
	{
		typedef decltype( make_unique( avformat_alloc_context(), &avformat_free_context ) ) pointer_type;
	
		pointer_type alloc_context( const io::context::pointer_type &ptr )
		{
			auto result = make_unique( avformat_alloc_context(), &avformat_free_context );
			if ( ptr )
			{
				result->pb = ptr->context_.get();
			}
			return result;
		}
		
		struct file
		{
			file( pointer_type &&f ) :
				format_( std::move( f ) ) {}
			
			file( file &&f ) = default;
			
			file( const file& ) = delete;
			
			bool decode( packet &p, AVFrame &frame )
			{
				if ( av::read_frame( format_, p ) )
				{
					av::decode( streams_[ p.stream_index ], p, frame );
				}
				else
				{
					AVPacket nill = { 0 };
					
					bool again = false;
					do
					{
						again = false;
						
						for ( auto &s : streams_ )
						{
							if ( s )
							{
								again |= av::decode( s, nill, frame );
							}
						}
					}
					while ( again );
					
					return false;
				}
				return true;
			}
			
			inline bool decode( packet &p, frame::pointer_type &frame )
			{
				return decode( p, *frame );
			}
			
			void decode_all( packet &&p, frame::pointer_type &&frame )
			{
				while ( decode( p, *frame ) )
				{
					//
				}
			}
			
			inline void decode_all()
			{
				decode_all( av::packet(), av::frame::alloc() );
			}
			
			void add_stream( AVStream &s )
			{
				callbacks_.push_back( {} );
				streams_.push_back( stream( s, callbacks_.back() ) );
			}
			
			template < AVMediaType T >
			struct stream_filter
			{
				bool operator()( const stream &s ) const
				{
					return s->codec->codec_type == T;
				}
			};
			
			std::vector< stream > streams( AVMediaType filter = AVMEDIA_TYPE_NB ) const
			{
				std::vector< stream > result;
				auto inserter = std::back_insert_iterator< std::vector< stream > >( result );
				auto s = streams_.begin(), e = streams_.end();
				
				switch( filter )
				{
					case AVMEDIA_TYPE_VIDEO:
						std::copy_if( s, e, inserter, stream_filter< AVMEDIA_TYPE_VIDEO >() );
						break;
					case AVMEDIA_TYPE_AUDIO:
						std::copy_if( s, e, inserter, stream_filter< AVMEDIA_TYPE_AUDIO >() );
						break;
					case AVMEDIA_TYPE_SUBTITLE:
						std::copy_if( s, e, inserter, stream_filter< AVMEDIA_TYPE_SUBTITLE >() );
						break;
					case AVMEDIA_TYPE_UNKNOWN:
						std::copy_if( s, e, inserter, stream_filter< AVMEDIA_TYPE_UNKNOWN >() );
						break;
					case AVMEDIA_TYPE_DATA:
						std::copy_if( s, e, inserter, stream_filter< AVMEDIA_TYPE_DATA >() );
						break;
					case AVMEDIA_TYPE_ATTACHMENT:
						std::copy_if( s, e, inserter, stream_filter< AVMEDIA_TYPE_ATTACHMENT >() );
						break;
					case AVMEDIA_TYPE_NB:
						result.assign( s, e );
						break;
				}
				return result;
			}
			
			private:
			
				pointer_type format_;
				std::vector< callback_t > callbacks_;
				std::vector< stream > streams_;
		};
		
		file open_input( const char *filename, pointer_type &&p, AVInputFormat *fmt = nullptr, AVDictionary **options = nullptr )
		{
			// release, instead of get since avformat_open_input will free ptr on error
			auto ptr = p.release();
			avformat_open_input( &ptr, filename, fmt, options ) < error();
			p.reset( ptr );
			
			file result( std::move( p ) );
			for ( auto i = 0; i < ptr->nb_streams; ++i )
			{
				if ( auto s = ptr->streams[ i ] )
				{
					s->discard = AVDISCARD_ALL;
					result.add_stream( *s );
				}
			}
			
			return result;
		}
		
		inline file open_input( const char *filename, AVInputFormat *fmt = nullptr, AVDictionary **options = nullptr )
		{
			return open_input( filename, alloc_context(), fmt, options );
		}
		
		inline file open_input( const char *filename, const io::context::pointer_type &ctx, AVInputFormat *fmt = nullptr, AVDictionary **options = nullptr )
		{
			return open_input( filename, alloc_context( ctx ), fmt, options );
		}
	}
}

namespace sws
{
	class context
	{
		std::vector< SwsContext* > contexts_;
	};
	
	void convert( AVFrame &frame, uint8_t **dst, const int *strides, AVPixelFormat desired, size_t width = 0, size_t height = 0, int flags = 0 )
	{
		if ( !width )
		{
			width = frame.width;
		}
		if ( !height )
		{
			height = frame.height;
		}
		
		auto ctx = sws_getCachedContext( nullptr, frame.width, frame.height, static_cast< AVPixelFormat >( frame.format ), width, height, desired, flags, nullptr, nullptr, nullptr );
		
		auto &picture = reinterpret_cast< AVPicture& >( frame );
		sws_scale( ctx, picture.data, picture.linesize, 0, frame.height, dst, strides );
	}
	
	void convert( AVFrame &frame, std::initializer_list< uint8_t* > dst, std::initializer_list< int > strides, AVPixelFormat desired, size_t width = 0, size_t height = 0, int flags = 0 )
	{
		std::vector< uint8_t* > dstptr( dst.begin(), dst.end() );
		dstptr.resize( AV_NUM_DATA_POINTERS, 0 );
		
		std::vector< int > stridesptr( strides.begin(), strides.end() );
		stridesptr.resize( AV_NUM_DATA_POINTERS, 0 );
		
		convert( frame, dstptr.data(), stridesptr.data(), desired, width, height, flags );
	}
	
	void convert( AVFrame &frame, void *dst, int stride, AVPixelFormat desired, size_t width = 0, size_t height = 0, int flags = 0 )
	{
		convert( frame, { reinterpret_cast< uint8_t* >( dst ) }, { stride }, desired, width, height, flags );
	}
}
