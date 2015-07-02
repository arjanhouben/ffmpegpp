#pragma once

#include <string>
#include <functional>
#include <memory>
#include <vector>
#include <array>
#include <iterator>
#include <algorithm>

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

	struct buffer
	{
		buffer( size_t s ) :
			data_( malloc( s ), &av_free ),
			size_( s ) { }

		buffer() :
			data_( nullptr, &av_free ),
			size_( 0 ) { }

        buffer& operator = ( buffer &&rhs )
        {
            data_ = std::move( rhs.data_ );
            size_ = rhs.size_;
            return *this;
        }

		unsigned char* data()
		{
			return reinterpret_cast< unsigned char* >( data_.get() );
		}

		const unsigned char* data() const
		{
			return reinterpret_cast< unsigned char* >( data_.get() );
		}

		size_t size() const
		{
			return size_;
		}

		private:

            buffer& operator = ( const buffer& );

			pointer_type data_;
			size_t size_;

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
	
	void setup_custom_io( AVFormatContext *c, const io::context::pointer_type &t );

	namespace format
	{
		struct context : std::unique_ptr< AVFormatContext, decltype( &avformat_free_context ) >
		{
            typedef std::unique_ptr< AVFormatContext, decltype( &avformat_free_context ) > base;

			explicit context( AVFormatContext *ctx, const io::context::pointer_type &ptr = io::context::pointer_type() ) :
                base( ctx, &avformat_free_context )
			{
				if ( ptr )
				{
					setup_custom_io( get(), ptr );
				}
			}
			
            explicit context( const io::context::pointer_type &ptr = io::context::pointer_type() ) :
                base( avformat_alloc_context(), &avformat_free_context )
            {
                if ( ptr )
                {
                    setup_custom_io( get(), ptr );
                }
            }

            context( context &&rhs ) = default;
			
			context& operator = ( context && ) = default;

            private:

                context( const context& );
		};
	}

	struct packet : AVPacket
	{
        inline static const AVPacket empty()
        {
            AVPacket p = { 0 };
            return p;
        }

		packet() :
            AVPacket( empty() )
		{
			av_init_packet( this );
		}

		~packet()
		{
			av_free_packet( this );
		}
	};

	bool read_frame( format::context &p, packet &pack )
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
		namespace helper
		{
			void free( AVCodecContext *ctx )
			{
				avcodec_free_context( &ctx );
			}
		}
		
		typedef std::unique_ptr< AVCodecContext, decltype( &helper::free ) > pointer_type;
	
		bool decode_video( AVCodecContext *codec, frame::pointer_type &p, const AVPacket &packet )
		{
			int frameFinished = false;
			avcodec_decode_video2( codec, p.get(), &frameFinished, &packet ) < error( "could not decode video" );
            return frameFinished != 0;
		}
		
		AVCodec* open( AVCodecContext &ctx )
		{
			AVCodec *decoder = nullptr;
			if ( !ctx.codec )
			{
				decoder = avcodec_find_decoder( ctx.codec_id );
			}
			avcodec_open2( &ctx, decoder, nullptr ) < av::error( "could not open codec" );
			return decoder;
		}
		
		AVCodec* open( const pointer_type &ctx )
		{
			return open( *ctx );
		}
		
		pointer_type context( const AVCodec *codec )
		{
			return make_unique( avcodec_alloc_context3( codec ), &helper::free );
		}
		
		pointer_type context( AVCodecID codec )
		{
			return context( avcodec_find_encoder( codec ) );
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

					type() :
						read( [](uint8_t*,int) { return 0; } ),
						write( [](uint8_t*,int) { return 0; } ),
						seek( [](int64_t,int) { return 0; } ),
						context_( nullptr, &av_free ),
						buffer_() {}

					std::function< int( uint8_t*, int ) > read, write;
					std::function< int64_t(int64_t,int) > seek;

					void buffer( buffer &&b )
					{
						buffer_ = std::move( b );
					}

					void context( AVIOContext *ctx )
					{
						context_.reset( ctx );
					}

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
					av::buffer buffer_;

					friend struct callback;
					friend void av::setup_custom_io( AVFormatContext *c, const io::context::pointer_type &t );
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
				auto result = std::unique_ptr< type >( new type() );
				auto ctx = avio_alloc_context( b.data(), b.size(), false, result.get(), &callback::read, &callback::write, &callback::seek );
				result->buffer( std::move( b ) );
				result->context( ctx );
				return result;
			}

			std::unique_ptr< type > alloc( size_t s = 4096 )
			{
				return alloc( buffer( s ) );
			}
		}
	}


	void setup_custom_io( AVFormatContext *c, const io::context::pointer_type &t )
	{
		c->pb = t->context_.get();
		c->flags = AVFMT_FLAG_CUSTOM_IO;
	}

	typedef std::function< void( AVFrame &frame ) > callback_t;

	struct stream
	{
		typedef std::unique_ptr< AVStream, decltype( &av_free ) > pointer_type;
		
		static void null_deleter( void* ) {}
		
		static pointer_type alloc( const format::context &fmt, const AVCodec *codec )
		{
			return make_unique( avformat_new_stream( fmt.get(), codec ), &av_free );
		}
		
		stream( pointer_type &&ptr ) :
            impl_( std::make_shared< implementation_t >( std::move( ptr ) ) ) {}
		
		stream( stream && ) = default;
		stream( const stream & ) = default;
		
		stream& operator = ( stream && ) = default;
		stream& operator = ( const stream & ) = default;
		
        operator bool() const
		{
			return bool( impl_->cb_ );
		}

		AVStream* get() const
		{
			return impl_->stream_.get();
		}

		AVStream* operator ->() const
		{
			return impl_->stream_.get();
		}

		void open( const callback_t &cb )
		{
			impl_->stream_->discard = AVDISCARD_DEFAULT;
			impl_->cb_ = cb;
			if ( impl_->stream_->codec )
			{
				codec::open( *impl_->stream_->codec );
			}
		}

		void close()
		{
			impl_->stream_->discard = AVDISCARD_ALL;
			impl_->cb_ = callback_t();
		}

		void call( AVFrame &frame )
		{
			impl_->cb_( frame );
		}

		private:
		
			struct implementation_t
			{
				implementation_t( pointer_type &&ptr ) :
					stream_( std::move( ptr ) ),
					cb_(),
					packet_(),
					frame_( frame::alloc() ){}
				
				pointer_type stream_;
				callback_t cb_;
				packet packet_;
				frame::pointer_type frame_;
			};
			std::shared_ptr< implementation_t > impl_;
	};

	bool encode( stream &stream, AVPacket &p, AVFrame &frame )
	{
		int frame_complete = false;
		switch( stream->codec->codec_type )
		{
			case AVMEDIA_TYPE_VIDEO:
			{
				stream.call( frame );
				auto result = avcodec_encode_video2( stream->codec, &p, &frame, &frame_complete );
				result < error( "could not encode video" );
                return frame_complete != 0;
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

	bool decode( stream &stream, AVPacket &p, AVFrame &frame )
	{
		int frame_complete = false;
		
		switch( stream->codec->codec_type )
		{
			case AVMEDIA_TYPE_VIDEO:
			{
				auto result = avcodec_decode_video2( stream->codec, &frame, &frame_complete, &p );
				p.size = 0;
				if ( result < 0 )
				{
					// error, do something usefull
				}
				if ( frame_complete )
				{
					stream.call( frame );
				}
                return result > 0;
			}
			case AVMEDIA_TYPE_AUDIO:
			{
				auto result = avcodec_decode_audio4( stream->codec, &frame, &frame_complete, &p );
				if ( result < 0 )
				{
					// error, do something usefull
					p.size = 0;
				}
				else
				{
					p.size -= result;
					p.data += result;
				}
				if ( frame_complete )
				{
					stream.call( frame );
				}
                return result > 0;
			}
			case AVMEDIA_TYPE_SUBTITLE:
			case AVMEDIA_TYPE_UNKNOWN:
			case AVMEDIA_TYPE_DATA:
			case AVMEDIA_TYPE_ATTACHMENT:
			case AVMEDIA_TYPE_NB:
				p.size = 0;
				break;
		}
		return false;
	}
	
	void interleaved_write_frame( format::context &fmt, packet &p )
	{
		av_interleaved_write_frame( fmt.get(), &p ) < error( "could not write frame" );
	}

	namespace format
	{
		template < AVMediaType compare, typename Iter, typename Output >
		void copy_if( Iter start, Iter end, Output out )
		{
			while ( start != end )
			{
				if ( ( *start )->codec->codec_type == compare )
				{
					*out = *start;
				}
				++start;
			}
		}

		struct file
		{
			file() :
				format_( nullptr ),
				streams_() {}
			
			file( context &&f ) :
				format_( std::move( f ) ),
				streams_() {}

            file( file &&rhs ) :
                format_( std::move( rhs.format_ ) ),
				streams_( std::move( rhs.streams_ ) ) {}
			
			file& operator = ( file &&rhs )
			{
				using std::swap;
				format_ = std::move( rhs.format_ );
				streams_ = std::move( rhs.streams_ );
				return *this;
			}
			
			
			bool encode( packet &p, AVFrame &frame )
			{
				if ( av::encode( streams_[ p.stream_index ], p, frame ) )
				{
					av::interleaved_write_frame( format_, p );
					return true;
				}
				return false;
			}
			
			inline bool encode( packet &p, frame::pointer_type &frame )
			{
				return encode( p, *frame );
			}

			void encode_all( packet &&p, frame::pointer_type &&frame )
			{
				for ( auto &s : streams_ )
				{
					p.stream_index = s->id;
					
					while ( encode( p, *frame ) )
					{
						//
					}
				}
			}

			bool decode( packet &p, AVFrame &frame )
			{
				if ( p.size || av::read_frame( format_, p ) )
				{
					av::decode( streams_[ p.stream_index ], p, frame );
				}
				else
				{
					AVPacket nill = { 0 };

					bool again = false;
					for ( auto &s : streams_ )
					{
						if ( s )
						{
							again |= av::decode( s, nill, frame );
						}
					}
					
					return again;
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

			void add_stream( AVStream *s )
			{
				streams_.push_back( stream( make_unique( s, &stream::null_deleter ) ) );
			}
			
			stream& add_stream( const AVCodec *codec )
			{
				auto str = av::stream::alloc( format_, codec );
				streams_.push_back( std::move( str ) );
				return streams_.back();
			}
			
			stream& add_stream( AVCodecID codec )
			{
				return add_stream( avcodec_find_encoder( codec ) );
			}

			std::vector< stream > streams( AVMediaType filter = AVMEDIA_TYPE_NB ) const
			{
				std::vector< stream > result;
				auto inserter = std::back_insert_iterator< std::vector< stream > >( result );
				auto s = streams_.begin(), e = streams_.end();

				switch( filter )
				{
					case AVMEDIA_TYPE_VIDEO:
						copy_if< AVMEDIA_TYPE_VIDEO >( s, e, inserter );
						break;
					case AVMEDIA_TYPE_AUDIO:
						copy_if< AVMEDIA_TYPE_AUDIO >( s, e, inserter );
						break;
					case AVMEDIA_TYPE_SUBTITLE:
						copy_if< AVMEDIA_TYPE_SUBTITLE >( s, e, inserter );
						break;
					case AVMEDIA_TYPE_UNKNOWN:
						copy_if< AVMEDIA_TYPE_UNKNOWN >( s, e, inserter );
						break;
					case AVMEDIA_TYPE_DATA:
						copy_if< AVMEDIA_TYPE_DATA >( s, e, inserter );
						break;
					case AVMEDIA_TYPE_ATTACHMENT:
						copy_if< AVMEDIA_TYPE_ATTACHMENT >( s, e, inserter );
						break;
					case AVMEDIA_TYPE_NB:
						result.assign( s, e );
						break;
				}
				return result;
			}
			
			void find_stream_info( AVDictionary **options = nullptr  )
			{
				avformat_find_stream_info( format_.get(), options ) < error( "could not find stream info" );
				
				for ( auto i = 0u; i < format_->nb_streams; ++i )
				{
					if ( auto s = format_->streams[ i ] )
					{
						s->discard = AVDISCARD_ALL;
						add_stream( s );
					}
				}
			}
		
			AVFormatContext* ctx() const
			{
				return format_.get();
			}

			private:

                file( const file& );

				context format_;
				std::vector< stream > streams_;
		};

		file open_input( const char *filename, context &&p, AVInputFormat *fmt = nullptr, AVDictionary **options = nullptr )
		{
			// release, instead of get since avformat_open_input will free ptr on error
			auto ptr = p.release();
			avformat_open_input( &ptr, filename, fmt, options ) < error( std::string( "open input: " ) + filename );
			p.reset( ptr );
			
			file result( std::move( p ) );
			
			result.find_stream_info( options );

			return result;
		}

		inline file open_input( const char *filename, AVInputFormat *fmt = nullptr, AVDictionary **options = nullptr )
		{
			return open_input( filename, context(), fmt, options );
		}

		inline file open_input( const char *filename, const io::context::pointer_type &ctx, AVInputFormat *fmt = nullptr, AVDictionary **options = nullptr )
		{
			return open_input( filename, context( ctx ), fmt, options );
		}

		inline file open_input( const io::context::pointer_type &ctx, AVInputFormat *fmt = nullptr, AVDictionary **options = nullptr )
		{
			return open_input( "", context( ctx ), fmt, options );
		}
		
		file open_output( const char *filename, context &&ptr )
		{
			auto ctx = ptr.get();
			avformat_alloc_output_context2( &ctx, nullptr, nullptr, filename ) < error("could not open output format" );
			return context( ptr.release() );
		}
	}
}

namespace sws
{
	class context
	{
		std::vector< SwsContext* > contexts_;
    };

    template < typename T >
    struct array_helper : std::array< T, AV_NUM_DATA_POINTERS >
    {
        typedef std::array< T, AV_NUM_DATA_POINTERS > base;

        array_helper( T v = T() ) :
            base()
        {
            base::operator []( 0 ) = v;
            std::fill( base::begin() + 1, base::end(), T() );
        }

        array_helper( T a, T b ) :
            base()
        {
            base::operator []( 0 ) = a;
            base::operator []( 1 ) = b;
            std::fill( base::begin() + 2, base::end(), T() );
        }

        array_helper( T a, T b, T c ) :
            base()
        {
            base::operator []( 0 ) = a;
            base::operator []( 1 ) = b;
            base::operator []( 2 ) = c;
            std::fill( base::begin() + 3, base::end(), T() );
        }

        array_helper( T a, T b, T c, T d ) :
            base()
        {
            base::operator []( 0 ) = a;
            base::operator []( 1 ) = b;
            base::operator []( 2 ) = c;
            base::operator []( 3 ) = d;
            std::fill( base::begin() + 4, base::end(), T() );
        }
    };

    typedef array_helper< int > strides_t;
    typedef array_helper< uint8_t* > pointers_t;

    void convert( AVFrame &frame, const pointers_t &dst, const strides_t &strides, AVPixelFormat desired, size_t width = 0, size_t height = 0, int flags = 0 )
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
        sws_scale( ctx, picture.data, picture.linesize, 0, frame.height, dst.data(), strides.data() );
	}

	void convert( AVFrame &frame, void *dst, int stride, AVPixelFormat desired, size_t width = 0, size_t height = 0, int flags = 0 )
	{
        convert( frame, pointers_t( reinterpret_cast< uint8_t* >( dst ) ), strides_t( stride ), desired, width, height, flags );
	}
}
