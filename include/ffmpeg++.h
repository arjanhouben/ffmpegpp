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
	
	template < typename T, typename D, void Destructor(D*) >
	struct wrapped_ptr
	{
		T *pointer_;
		void (*destructor_)(D*);
		
		wrapped_ptr( T *t = nullptr, void d(D*) = Destructor ) :
			pointer_( t ),
			destructor_( d )
		{
		}
		
		wrapped_ptr( wrapped_ptr &&rhs ) :
			pointer_( rhs.release() ),
			destructor_( rhs.destructor_ ) {}
		
		wrapped_ptr& operator = ( wrapped_ptr &&rhs )
		{
			reset( rhs.release() );
			destructor_ = rhs.destructor_;
			return *this;
		}
		
		wrapped_ptr( const wrapped_ptr& ) = delete;
		wrapped_ptr& operator = ( const wrapped_ptr& ) = delete;
		
		T* get() const
		{
			return pointer_;
		}
		
		T* operator -> ()
		{
			return pointer_;
		}
		
		typename std::add_lvalue_reference< T >::type operator *()
		{
			return *pointer_;
		}
		
		void reset( T *t = nullptr )
		{
			auto old = pointer_;
			pointer_ = t;
		}
		
		T* release()
		{
			auto old = pointer_;
			pointer_ = nullptr;
			return old;
		}
		
		~wrapped_ptr()
		{
			reset();
		}
	};
	

		#if 0
	template < typename T, typename D, void Destructor(D*) >
	struct wrapped_ptr : std::unique_ptr< T, decltype( Destructor ) >
	{
		typedef std::unique_ptr< T, decltype( Destructor ) > type;
		
		wrapped_ptr( T *t = nullptr ) :
		type( t, Destructor )
		{
		}
		
		wrapped_ptr( type &&base ) :
			type( std::move( base ) )
		{
		}

		virtual ~wrapped_ptr()
		{
		}

		wrapped_ptr( wrapped_ptr && ) = default;

		wrapped_ptr& operator = ( wrapped_ptr &&rhs )
		{
			{
				henk() << "assign:\t" << this << std::endl;
			}
			type::operator=( std::move( rhs ) );
			return *this;
		}
	};
	#endif
	
	struct buffer
	{
		buffer( size_t s ) :
			data_( av_malloc( s ) ),
			size_( s ) { }

		buffer() :
			data_(),
			size_( 0 ) { }
		
		buffer( buffer &&rhs ) = default;

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

			typedef wrapped_ptr< void, void, &av_free > data_type;

			data_type data_;
			size_t size_;

	};

	namespace format
	{
		typedef wrapped_ptr< AVFormatContext, AVFormatContext, &avformat_free_context > context;
	}
	
	namespace io
	{
		namespace context
		{
			class type;
		}
	}
	
	namespace format
	{
		context make_context( const io::context::type &t );
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

		typedef wrapped_ptr< AVFrame, AVFrame, &free > frame;

		frame alloc()
		{
			return av_frame_alloc();
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
		
		typedef wrapped_ptr< AVCodecContext, AVCodecContext, &helper::free > context;
	
		bool decode_video( AVCodecContext *codec, frame::frame &p, const AVPacket &packet )
		{
			int frameFinished = false;
			avcodec_decode_video2( codec, p.get(), &frameFinished, &packet ) < error( "could not decode video" );
            return frameFinished != 0;
		}
		
		AVCodec* open_input( AVCodecContext &ctx )
		{
			AVCodec *decoder = nullptr;
			if ( !ctx.codec )
			{
				decoder = avcodec_find_decoder( ctx.codec_id );
			}
			avcodec_open2( &ctx, decoder, nullptr ) < av::error( "could not open codec" );
			return decoder;
		}
		
		AVCodec* open_output( AVCodecContext &ctx )
		{
			AVCodec *decoder = nullptr;
			if ( !ctx.codec )
			{
				decoder = avcodec_find_encoder( ctx.codec_id );
			}
			avcodec_open2( &ctx, decoder, nullptr ) < av::error( "could not open codec" );
			return decoder;
		}
		
		context make_context( const AVCodec *codec )
		{
			return context( avcodec_alloc_context3( codec ) );
		}
		
		context make_context( AVCodecID codec )
		{
			return make_context( avcodec_find_encoder( codec ) );
		}
	}

	namespace io
	{
		namespace context
		{
			typedef wrapped_ptr< AVIOContext, void, &av_free > AVIOContextPtr;


			namespace callback
			{
				static int read( void *p, uint8_t *b, int s );

				static int write( void *p, uint8_t *b, int s );

				static int64_t seek( void *p, int64_t b, int s );
			};
			
			class type : public AVIOContextPtr
			{
				public:

					type() :
						AVIOContextPtr(),
						read( [](uint8_t*,int) { return 0; } ),
						write( [](uint8_t*,int) { return 0; } ),
						seek( [](int64_t,int) { return 0; } ),
						buffer_() {}
				
					type( AVIOContextPtr &&ctx ) :
						AVIOContextPtr( std::move( ctx ) ),
						read( [](uint8_t*,int) { return 0; } ),
						write( [](uint8_t*,int) { return 0; } ),
						seek( [](int64_t,int) { return 0; } ),
						buffer_() {}
				
					type( buffer &&b ) :
						AVIOContextPtr(),
						read( [](uint8_t*,int) { return 0; } ),
						write( [](uint8_t*,int) { return 0; } ),
						seek( [](int64_t,int) { return 0; } ),
						buffer_( std::move( b ) )
					{
						reset( avio_alloc_context( b.data(), b.size(), false, this, &callback::read, &callback::write, &callback::seek ) );
					}

				
					type( type&& ) {}
				
					operator bool() const
					{
						return AVIOContextPtr::get();
					}
				
//					type& operator = ( type&& ) {}

					std::function< int( uint8_t*, int ) > read, write;
					std::function< int64_t(int64_t,int) > seek;

					void buffer( buffer &&b )
					{
						buffer_ = std::move( b );
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

					av::buffer buffer_;

					friend int callback::read( void*, uint8_t*, int );
					friend int callback::write( void*, uint8_t*, int );
					friend int64_t callback::seek( void*, int64_t, int );
			};

			namespace callback
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

			type alloc( buffer &&b )
			{
				return type( std::move( b ) );
			}

			type alloc( size_t s = 4096 )
			{
				return alloc( buffer( s ) );
			}
		}
	}
	
	namespace format
	{
		context make_context( const io::context::type &t = io::context::type() )
		{
			auto ctx = avformat_alloc_context();
			if ( t )
			{
				ctx->pb = t.get();
				ctx->flags = AVFMT_FLAG_CUSTOM_IO;
			}
			return ctx;
		}
	}

	typedef std::function< bool( AVFrame &frame ) > callback_t;

	struct stream
	{
		typedef wrapped_ptr< AVStream, void, &av_free > stream_type;

		static void null_deleter( void* ) {}
		
		stream( const format::context &fmt, const AVCodec *codec ) :
            impl_( std::make_shared< implementation_t >( avformat_new_stream( fmt.get(), codec ) ) ) {}

		explicit stream( AVStream *ptr = nullptr ) :
            impl_( std::make_shared< implementation_t >( ptr ) ) {}
		
//		stream( stream && ) = default;
//		stream( const stream & ) = default;
//		
//		stream& operator = ( stream && ) = default;
//		stream& operator = ( const stream & ) = default;
//		
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

		void open_output( const callback_t &cb )
		{
			impl_->stream_->discard = AVDISCARD_DEFAULT;
			impl_->cb_ = cb;
			if ( impl_->stream_->codec )
			{
				codec::open_output( *impl_->stream_->codec );
			}
		}

		void open_input( const callback_t &cb )
		{
			impl_->stream_->discard = AVDISCARD_DEFAULT;
			impl_->cb_ = cb;
			if ( impl_->stream_->codec )
			{
				codec::open_input( *impl_->stream_->codec );
			}
		}

		void close()
		{
			impl_->stream_->discard = AVDISCARD_ALL;
			impl_->cb_ = callback_t();
		}

		bool call( AVFrame &frame )
		{
			return impl_->cb_( frame );
		}

		private:
		
			struct implementation_t
			{
				implementation_t( AVStream *ptr = nullptr ) :
					stream_( ptr, &null_deleter ),
					cb_(),
					packet_(),
					frame_( frame::alloc() ){}
				
				implementation_t( stream_type &&ptr ) :
					stream_( std::move( ptr ) ),
					cb_(),
					packet_(),
					frame_( frame::alloc() ) {}
				
				stream_type stream_;
				callback_t cb_;
				packet packet_;
				frame::frame frame_;
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
				if ( stream.call( frame ) )
				{
					auto result = avcodec_encode_video2( stream->codec, &p, &frame, &frame_complete );
					result = avcodec_encode_video2( stream->codec, &p, nullptr, &frame_complete );
					result < error( "could not encode video" );
				}
				return frame_complete != false;
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
				format_(),
				streams_() {}
			
			file( context &&f ) :
				format_( std::move( f ) ),
				streams_() {}

            file( file &&rhs ) :
				format_( std::move( rhs.format_ ) ),
				streams_( std::move( rhs.streams_ ) ) {}
			
			file& operator = ( file &&rhs )
			{
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
			
			inline bool encode( packet &p, frame::frame &frame )
			{
				return encode( p, *frame );
			}

			void encode_all( packet &&p, frame::frame &&frame )
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

			inline bool decode( packet &p, frame::frame &frame )
			{
				return decode( p, *frame );
			}

			void decode_all( packet &&p, frame::frame &&frame )
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
				streams_.push_back( stream( s ) );
			}
			
			stream& add_stream( const AVCodec &codec )
			{
				streams_.push_back( stream( format_, &codec ) );
				return streams_.back();
			}
			
			stream& add_stream( AVCodecID codecid )
			{
				if ( auto codec = avcodec_find_encoder( codecid ) )
				{
					return add_stream( *codec );
				}
				using std::to_string;
				throw std::runtime_error( "could not find codec for id: " + to_string( codecid ) );
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
			// release, instead of get, since avformat_open_input will free ptr on error
			auto ptr = p.release();
			avformat_open_input( &ptr, filename, fmt, options ) < error( std::string( "open input: " ) + filename );
			p.reset( ptr );
			
			file result( std::move( p ) );
			
			result.find_stream_info( options );

			return result;
		}

		inline file open_input( const char *filename, AVInputFormat *fmt = nullptr, AVDictionary **options = nullptr )
		{
			return open_input( filename, av::format::make_context(), fmt, options );
		}

		inline file open_input( const char *filename, const io::context::type &ctx, AVInputFormat *fmt = nullptr, AVDictionary **options = nullptr )
		{
			return open_input( filename, make_context( ctx ), fmt, options );
		}

		inline file open_input( const io::context::type &ctx, AVInputFormat *fmt = nullptr, AVDictionary **options = nullptr )
		{
			return open_input( "", make_context( ctx ), fmt, options );
		}
		
		file open_output( const char *filename )
		{
			AVFormatContext *ctx = nullptr;
			avformat_alloc_output_context2( &ctx, nullptr, nullptr, filename ) < error( "could not open output format" );
			
			if ( !( ctx->flags & AVFMT_NOFILE ) )
			{
				avio_open( &ctx->pb, filename, AVIO_FLAG_WRITE ) < error( "could not open output file" );
			}

			return file( context( ctx ) );
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
		
		template < typename Iterator >
		array_helper( Iterator a, Iterator b ) :
			base()
		{
			auto count = std::min< size_t >( std::distance( a, b ), AV_NUM_DATA_POINTERS );
			std::copy( a, a + count, base::begin() );
			std::fill( base::begin() + count, base::end(), T() );
		}

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
	
	template < typename Destination, typename Source >
	void assign_if_null( Destination &dest, const Source &src )
	{
		if ( !dest )
		{
			dest = src;
		}
	}
	
	struct helper
	{
		void to_avframe( AVFrame &f ) const
		{
			std::copy( stride.begin(), stride.end(), f.linesize );
			std::copy( data.begin(), data.end(), f.data );
			f.format = format;
			f.width = width;
			f.height = height;
		}
		void to_avframe( av::frame::frame &f ) const
		{
			to_avframe( *f );
		}
		av::frame::frame to_avframe() const
		{
			auto f = av::frame::alloc();
			to_avframe( f );
			return f;
		}
		strides_t stride;
		pointers_t data;
		AVPixelFormat format;
		size_t width, height;
	};

    void convert( const helper &src, helper &dst, int flags = 0 )
	{
		auto ctx = sws_getCachedContext( nullptr, src.width, src.height, src.format, dst.width, dst.height, dst.format, flags, nullptr, nullptr, nullptr );
		
        sws_scale( ctx, src.data.data(), src.stride.data(), 0, src.height, dst.data.data(), dst.stride.data() );
		
		sws_freeContext( ctx );
	}

    void convert( AVFrame &frame, const pointers_t &dst, const strides_t &strides, AVPixelFormat desired, size_t width = 0, size_t height = 0, int flags = 0 )
	{
		assign_if_null( width, frame.width );
		assign_if_null( height, frame.height );

		auto ctx = sws_getCachedContext( nullptr, frame.width, frame.height, static_cast< AVPixelFormat >( frame.format ), width, height, desired, flags, nullptr, nullptr, nullptr );

		auto &picture = reinterpret_cast< AVPicture& >( frame );
        sws_scale( ctx, picture.data, picture.linesize, 0, frame.height, dst.data(), strides.data() );
	}

	void convert( AVFrame &frame, void *dst, int stride, AVPixelFormat desired, size_t width = 0, size_t height = 0, int flags = 0 )
	{
        convert( frame, pointers_t( reinterpret_cast< uint8_t* >( dst ) ), strides_t( stride ), desired, width, height, flags );
	}
}
