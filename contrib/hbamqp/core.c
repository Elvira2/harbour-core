/* Requires: rabbitmq-c 0.4.0 or upper */

#include "hbapi.h"
#include "hbapierr.h"
#include "hbapiitm.h"

#include <amqp.h>
#include <amqp_framing.h>
#include <amqp_tcp_socket.h>
#include <amqp_ssl_socket.h>

#if defined(HB_OS_WIN) && defined(__MINGW32__)
#include <sys/time.h>
#endif

#define HB_AMQP_VERS( ma, mi, mu )  \
   ( AMQP_VERSION_MAJOR > ma || \
   ( AMQP_VERSION_MAJOR == ma && \
   ( AMQP_VERSION_MINOR > mi || \
   ( AMQP_VERSION_MINOR == mi && \
     AMQP_VERSION_PATCH >= mu ) ) ) )

/* object destructor, it's executed automatically */
static HB_GARBAGE_FUNC( hb_amq_connection_Destructor )
{
   /* Retrieve object pointer holder */
   amqp_connection_state_t * ptr = ( amqp_connection_state_t * ) Cargo;

   /* Check if pointer is not NULL to avoid multiple freeing */
   if( *ptr )
   {
      /* Destroy the object */
      amqp_destroy_connection( *ptr );

      /* set pointer to NULL to avoid multiple freeing */
      *ptr = NULL;
   }
}

static const HB_GC_FUNCS s_gc_amq_connection_Funcs =
{
   hb_amq_connection_Destructor,
   hb_gcDummyMark
};

/* function returns object pointer or NULL when wrong variable is
   passed or object was freed before */
static amqp_connection_state_t hb_par_amq_connection( int iParam )
{
   amqp_connection_state_t * ptr = ( amqp_connection_state_t * ) hb_parptrGC( &s_gc_amq_connection_Funcs, iParam );

   return ptr ? *ptr : NULL;
}

/* HB_ENVELOPE destructor, it's executed automatically */
static HB_GARBAGE_FUNC( hb_amq_envelope_Destructor )
{
   /* Retrieve object pointer holder */
   amqp_envelope_t ** ptr = ( amqp_envelope_t ** ) Cargo;

   /* Check if pointer is not NULL to avoid multiple freeing */
   if( *ptr )
   {
      /* Destroy the object */
      amqp_destroy_envelope( *ptr );

      hb_xfree( *ptr );

      /* set pointer to NULL to avoid multiple freeing */
      *ptr = NULL;
   }
}

static const HB_GC_FUNCS s_gc_amq_envelope_Funcs =
{
   hb_amq_envelope_Destructor,
   hb_gcDummyMark
};

/* function returns object pointer or NULL when wrong variable is
   passed or object was freed before */
static amqp_envelope_t * hb_par_amq_envelope( int iParam )
{
   amqp_envelope_t ** ptr = ( amqp_envelope_t ** ) hb_parptrGC( &s_gc_amq_envelope_Funcs, iParam );

   return ptr ? *ptr : NULL;
}

static amqp_response_type_enum s_decode_reply( amqp_rpc_reply_t x, char const * context )
{
   switch( x.reply_type )
   {
      case AMQP_RESPONSE_NORMAL:
         break;

      case AMQP_RESPONSE_NONE:
         HB_TRACE( HB_TR_ERROR, ( "amqp - %s: missing RPC reply type!", context ) );
         break;

      case AMQP_RESPONSE_LIBRARY_EXCEPTION:
         HB_TRACE( HB_TR_ERROR, ( "amqp - %s: %d %s", context, x.library_error, amqp_error_string2( x.library_error ) ) );
         break;

      case AMQP_RESPONSE_SERVER_EXCEPTION:
         switch( x.reply.id )
         {
            case AMQP_CONNECTION_CLOSE_METHOD:
            {
               amqp_connection_close_t * m = ( amqp_connection_close_t * ) x.reply.decoded;
               HB_TRACE( HB_TR_ERROR, ( "amqp - %s: server connection error %d, message: %.*s",
                                        context,
                                        m->reply_code,
                                        ( int ) m->reply_text.len, ( const char * ) m->reply_text.bytes ) );

               break;
            }
            case AMQP_CHANNEL_CLOSE_METHOD:
            {
               amqp_channel_close_t * m = ( amqp_channel_close_t * ) x.reply.decoded;

               HB_TRACE( HB_TR_ERROR, ( "amqp - %s: server channel error %d, message: %.*s",
                                        context,
                                        m->reply_code,
                                        ( int ) m->reply_text.len, ( const char * ) m->reply_text.bytes ) );
               break;
            }
            default:
               HB_TRACE( HB_TR_ERROR, ( "amqp - %s: unrecognized server error, method id 0x%08X", context, x.reply.id ) );
               break;
         }
         break;
#if 0
      default:
         HB_TRACE( HB_TR_ERROR, ( "amqp - %s: unrecognized reply type %d", context, x.reply_type ) );
         break;
#endif
   }

   return x.reply_type;
}

static int s_decode_status( int status, char const * context )
{
   if( status != AMQP_STATUS_OK )
   {
      HB_TRACE( HB_TR_ERROR, ( "amqp - %s status=%d (%s)", context, status, amqp_error_string2( status ) ) );
   }

   return status;
}

/* Creates a new amqp_connection_state_t object.
   amqp_connection_state_t objects created with this function should be freed with amqp_destroy_connection()
   amqp_new_connection() --> pConn */
HB_FUNC( AMQP_NEW_CONNECTION )
{
   amqp_connection_state_t conn = amqp_new_connection();

   if( conn )
   {
      amqp_connection_state_t * ptr = ( amqp_connection_state_t * ) hb_gcAllocate( sizeof( amqp_connection_state_t * ),
                                                                                   &s_gc_amq_connection_Funcs );
      *ptr = conn;

      hb_retptrGC( ( void * ) ptr );
   }
   else
      hb_retptr( NULL );
}

/* amqp_connection_close( pConn, nReason ) --> nResponse */
HB_FUNC( AMQP_CONNECTION_CLOSE )
{
   amqp_connection_state_t conn = hb_par_amq_connection( 1 );

   if( conn )
      hb_retni( s_decode_reply(
         amqp_connection_close(
            conn,
            hb_parnidef( 2, AMQP_REPLY_SUCCESS ) /* reason */ ),
         "amqp_connection_close()" ) );
   else
      hb_errRT_BASE( EG_ARG, 3012, NULL, HB_ERR_FUNCNAME, HB_ERR_ARGS_BASEPARAMS );
}

/* amqp_tcp_socket_new( pConn ) --> pSocket */
HB_FUNC( AMQP_TCP_SOCKET_NEW )
{
   amqp_connection_state_t conn = hb_par_amq_connection( 1 );

   if( conn )
      hb_retptr( amqp_tcp_socket_new( conn ) );
   else
      hb_errRT_BASE( EG_ARG, 3012, NULL, HB_ERR_FUNCNAME, HB_ERR_ARGS_BASEPARAMS );
}

/* amqp_set_initialize_ssl_library( lDoInitialize ) */
HB_FUNC( AMQP_SET_INITIALIZE_SSL_LIBRARY )
{
   amqp_set_initialize_ssl_library( ( amqp_boolean_t ) hb_parl( 1 ) /* do_initialize */ );
}

/* amqp_ssl_socket_new( pConn ) --> pSocket */
HB_FUNC( AMQP_SSL_SOCKET_NEW )
{
   amqp_connection_state_t conn = hb_par_amq_connection( 1 );

   if( conn )
      hb_retptr( amqp_ssl_socket_new( conn ) );
   else
      hb_errRT_BASE( EG_ARG, 3012, NULL, HB_ERR_FUNCNAME, HB_ERR_ARGS_BASEPARAMS );
}

/* amqp_ssl_socket_set_cacert( pSocket, cCACertFileName_PEM ) --> nStatus */
HB_FUNC( AMQP_SSL_SOCKET_SET_CACERT )
{
   amqp_socket_t * pSocket = ( amqp_socket_t * ) hb_parptr( 1 );

   if( pSocket )
      hb_retni( amqp_ssl_socket_set_cacert( pSocket, hb_parcx( 2 ) ) );
   else
      hb_errRT_BASE( EG_ARG, 3012, NULL, HB_ERR_FUNCNAME, HB_ERR_ARGS_BASEPARAMS );
}

/* amqp_ssl_socket_set_key( pSocket, cClientCertFileName_PEM, cClientKeyFileName_PEM ) --> nStatus */
HB_FUNC( AMQP_SSL_SOCKET_SET_KEY )
{
   amqp_socket_t * pSocket = ( amqp_socket_t * ) hb_parptr( 1 );

   if( pSocket )
      hb_retni( amqp_ssl_socket_set_key( pSocket, hb_parcx( 2 ), hb_parcx( 3 ) ) );
   else
      hb_errRT_BASE( EG_ARG, 3012, NULL, HB_ERR_FUNCNAME, HB_ERR_ARGS_BASEPARAMS );
}

/* amqp_ssl_socket_set_ssl_versions( pSocket, nVersionMin, nVersionMax ) */
HB_FUNC( AMQP_SSL_SOCKET_SET_SSL_VERSIONS )
{
   amqp_socket_t * pSocket = ( amqp_socket_t * ) hb_parptr( 1 );

   if( pSocket )
#if HB_AMQP_VERS( 0, 8, 0 )
      hb_retni( amqp_ssl_socket_set_ssl_versions(
         pSocket,
         ( amqp_tls_version_t ) hb_parni( 2 ),
         ( amqp_tls_version_t ) hb_parni( 3 ) ) );
#else
      hb_retni( AMQP_STATUS_OK );
#endif
   else
      hb_errRT_BASE( EG_ARG, 3012, NULL, HB_ERR_FUNCNAME, HB_ERR_ARGS_BASEPARAMS );
}

/* amqp_ssl_socket_set_verify_peer( pSocket, lVerify ) */
HB_FUNC( AMQP_SSL_SOCKET_SET_VERIFY_PEER )
{
   amqp_socket_t * pSocket = ( amqp_socket_t * ) hb_parptr( 1 );

   if( pSocket )
#if HB_AMQP_VERS( 0, 8, 0 )
      amqp_ssl_socket_set_verify_peer( pSocket, ( amqp_boolean_t ) hb_parl( 2 ) /* verify */ );
#else
      hb_ret();
#endif
   else
      hb_errRT_BASE( EG_ARG, 3012, NULL, HB_ERR_FUNCNAME, HB_ERR_ARGS_BASEPARAMS );
}

/* amqp_ssl_socket_set_verify_hostname( pSocket, lVerify ) */
HB_FUNC( AMQP_SSL_SOCKET_SET_VERIFY_HOSTNAME )
{
   amqp_socket_t * pSocket = ( amqp_socket_t * ) hb_parptr( 1 );

   if( pSocket )
#if HB_AMQP_VERS( 0, 8, 0 )
      amqp_ssl_socket_set_verify_hostname( pSocket, ( amqp_boolean_t ) hb_parl( 2 ) /* verify */ );
#else
      hb_ret();
#endif
   else
      hb_errRT_BASE( EG_ARG, 3012, NULL, HB_ERR_FUNCNAME, HB_ERR_ARGS_BASEPARAMS );
}

/* Attempts to open a socket to hostname on portnumber
   amqp_socket_open( pSocket, cHost, nPort ) --> nStatus */
HB_FUNC( AMQP_SOCKET_OPEN )
{
   amqp_socket_t * pSocket = ( amqp_socket_t * ) hb_parptr( 1 );

   if( pSocket )
      hb_retni( amqp_socket_open( pSocket, hb_parcx( 2 ), hb_parni( 3 ) /* port */ ) );
   else
      hb_errRT_BASE( EG_ARG, 3012, NULL, HB_ERR_FUNCNAME, HB_ERR_ARGS_BASEPARAMS );
}

/* Login to the broker.
   After using amqp_open_socket() and amqp_set_sockfd(), call amqp_login() to complete connecting to the broker
   amqp_login( pConn, cVHost, nFrameSize, nMethod, user, pwd ) --> nResponse */
HB_FUNC( AMQP_LOGIN )
{
   amqp_connection_state_t conn = hb_par_amq_connection( 1 );

   if( conn )
      hb_retni( s_decode_reply(
         amqp_login(
            conn,
            hb_parcx( 2 ) /* vhost */,
            0 /* ch max */,
            hb_parnidef( 3, 0x20000 ) /* frame size */,
            0 /* heartbeat  - unsupported */,
            ( amqp_sasl_method_enum ) hb_parni( 4 ) /* AMQP_SASL_METHOD_PLAIN */,
            hb_parcx( 5 ) /* user */,
            hb_parcx( 6 ) /* pwd */ ),
         "amqp_login()" ) );
   else
      hb_errRT_BASE( EG_ARG, 3012, NULL, HB_ERR_FUNCNAME, HB_ERR_ARGS_BASEPARAMS );
}

/* amqp_channel_open( pConn, nChannel ) --> nResponse */
HB_FUNC( AMQP_CHANNEL_OPEN )
{
   amqp_connection_state_t conn = hb_par_amq_connection( 1 );

   if( conn )
   {
      amqp_channel_open( conn, ( amqp_channel_t ) hb_parnidef( 2, 1 ) );

      hb_retni( s_decode_reply( amqp_get_rpc_reply( conn ), "amqp_channel_open()" ) );
   }
   else
      hb_errRT_BASE( EG_ARG, 3012, NULL, HB_ERR_FUNCNAME, HB_ERR_ARGS_BASEPARAMS );
}

/* amqp_channel_close( pConn, nChannel ) --> nResponse */
HB_FUNC( AMQP_CHANNEL_CLOSE )
{
   amqp_connection_state_t conn = hb_par_amq_connection( 1 );

   if( conn )
   {
      hb_retni( s_decode_reply(
         amqp_channel_close(
            conn,
            ( amqp_channel_t ) hb_parnidef( 2, 1 ),
            AMQP_REPLY_SUCCESS /* code */ ),
         "amqp_channel_close()" ) );
   }
   else
      hb_errRT_BASE( EG_ARG, 3012, NULL, HB_ERR_FUNCNAME, HB_ERR_ARGS_BASEPARAMS );
}

/* amqp_exchange_declare( pConn, nChannel, cExchange, cExchangeType, lPassive, lDurable, lAutoDelete, lInternal ) --> nResponse */
HB_FUNC( AMQP_EXCHANGE_DECLARE )
{
   amqp_connection_state_t conn = hb_par_amq_connection( 1 );

   if( conn )
   {
      amqp_exchange_declare(
         conn,
         ( amqp_channel_t ) hb_parnidef( 2, 1 ),  /* channel */
         amqp_cstring_bytes( hb_parcx( 3 ) ),     /* exchange */
         amqp_cstring_bytes( hb_parcx( 4 ) ),     /* type */
         ( amqp_boolean_t ) hb_parl( 5 ),         /* passive */
         ( amqp_boolean_t ) hb_parl( 6 ),         /* durable */
#if HB_AMQP_VERS( 0, 6, 0 )
         ( amqp_boolean_t ) hb_parl( 7 ),         /* auto_delete */
         ( amqp_boolean_t ) hb_parl( 8 ),         /* internal */
#endif
         amqp_empty_table );

      hb_retni( s_decode_reply(
         amqp_get_rpc_reply(
            conn ),
         "amqp_exchange_declare()" ) );
   }
   else
      hb_errRT_BASE( EG_ARG, 3012, NULL, HB_ERR_FUNCNAME, HB_ERR_ARGS_BASEPARAMS );
}

/* Publish a message to the broker.
   amqp_basic_publish( pConn, nChannel, cExchange, cKey, nMandatory, nImmediate, hProperties, cBody ) --> nStatus
   - hProperties keys: content_type, delivery_mode */
HB_FUNC( AMQP_BASIC_PUBLISH )
{
   amqp_connection_state_t conn = hb_par_amq_connection( 1 );

   if( conn && HB_ISHASH( 7 ) )
   {
      PHB_ITEM pProps = hb_param( 7, HB_IT_HASH );

      amqp_basic_properties_t props;

      props._flags =
         AMQP_BASIC_CONTENT_TYPE_FLAG |
         AMQP_BASIC_DELIVERY_MODE_FLAG;

      props.content_type  = amqp_cstring_bytes( hb_itemGetCPtr( hb_hashGetCItemPtr( pProps, "content_type" ) ) );
      props.delivery_mode = hb_itemGetNI( hb_hashGetCItemPtr( pProps, "delivery_mode" ) );  /* AMQP_DELIVERY_* */

      hb_retni( s_decode_status(
         amqp_basic_publish(
            conn,
            ( amqp_channel_t ) hb_parnidef( 2, 1 ),  /* channel */
            amqp_cstring_bytes( hb_parcx( 3 ) ),     /* exchange */
            amqp_cstring_bytes( hb_parcx( 4 ) ),     /* routing_key */
            hb_parni( 5 ),                           /* mandatory */
            hb_parni( 6 ),                           /* immediate */
            &props,                                  /* properties */
            amqp_cstring_bytes( hb_parcx( 8 ) ) ),   /* body */
         "amqp_basic_publish()" ) );
   }
   else
      hb_errRT_BASE( EG_ARG, 3012, NULL, HB_ERR_FUNCNAME, HB_ERR_ARGS_BASEPARAMS );
}

/* amqp_basic_consume( pConn, nChannel, cQueueName, cConsumerTag, lNoLocal, lNoAck, lExclusive ) --> nResponse */
HB_FUNC( AMQP_BASIC_CONSUME )
{
   amqp_connection_state_t conn = hb_par_amq_connection( 1 );

   if( conn )
   {
      amqp_basic_consume(
         conn,
         ( amqp_channel_t ) hb_parnidef( 2, 1 ),  /* channel */
         amqp_cstring_bytes( hb_parcx( 3 ) ),     /* queuename */
         amqp_cstring_bytes( hb_parcx( 4 ) ),     /* consumer_tag */
         ( amqp_boolean_t ) hb_parl( 5 ),         /* no_local */
         ( amqp_boolean_t ) hb_parl( 6 ),         /* no_ack */
         ( amqp_boolean_t ) hb_parl( 7 ),         /* exclusive */
         amqp_empty_table );

      hb_retni( s_decode_reply( amqp_get_rpc_reply( conn ), "amqp_basic_consume()" ) );
   }
   else
      hb_errRT_BASE( EG_ARG, 3012, NULL, HB_ERR_FUNCNAME, HB_ERR_ARGS_BASEPARAMS );
}

/* Acknowledge message(s)
   amqp_basic_ack( pConn, nChannel, nDeliveryTag, lMultiple ) --> nResponse */
HB_FUNC( AMQP_BASIC_ACK )
{
   amqp_connection_state_t conn = hb_par_amq_connection( 1 );

   if( conn )
      hb_retni( amqp_basic_ack(
         conn /* state */,
         ( amqp_channel_t ) hb_parnidef( 2, 1 ) /* channel */,
         hb_parnint( 3 ) /* delivery_tag */,
         ( amqp_boolean_t ) hb_parl( 4 ) /* multiple */ ) );
   else
      hb_errRT_BASE( EG_ARG, 3012, NULL, HB_ERR_FUNCNAME, HB_ERR_ARGS_BASEPARAMS );
}

/* Reject message(s)
   amqp_basic_nack( pConn, nChannel, nDeliveryTag, lMultiple, lRequeue ) --> nResponse */
HB_FUNC( AMQP_BASIC_NACK )
{
   amqp_connection_state_t conn = hb_par_amq_connection( 1 );

   if( conn )
#if HB_AMQP_VERS( 0, 5, 0 )
      hb_retni( amqp_basic_nack(
         conn /* state */,
         ( amqp_channel_t ) hb_parnidef( 2, 1 ) /* channel */,
         hb_parnint( 3 ) /* delivery_tag */,
         ( amqp_boolean_t ) hb_parl( 4 ) /* multiple */,
         ( amqp_boolean_t ) hb_parl( 4 ) /* requeue */ ) );
#else
      hb_retni( AMQP_STATUS_OK );
#endif
   else
      hb_errRT_BASE( EG_ARG, 3012, NULL, HB_ERR_FUNCNAME, HB_ERR_ARGS_BASEPARAMS );
}

/* Reject a message
   amqp_basic_reject( pConn, nChannel, nDeliveryTag, lRequeue ) --> nResponse */
HB_FUNC( AMQP_BASIC_REJECT )
{
   amqp_connection_state_t conn = hb_par_amq_connection( 1 );

   if( conn )
      hb_retni( amqp_basic_reject(
         conn /* state */,
         ( amqp_channel_t ) hb_parnidef( 2, 1 ) /* channel */,
         hb_parnint( 3 ) /* delivery_tag */,
         ( amqp_boolean_t ) hb_parl( 4 ) /* requeue */ ) );
   else
      hb_errRT_BASE( EG_ARG, 3012, NULL, HB_ERR_FUNCNAME, HB_ERR_ARGS_BASEPARAMS );
}

/* amqp_frames_enqueued( pConn ) --> lValue */
HB_FUNC( AMQP_FRAMES_ENQUEUED )
{
   amqp_connection_state_t conn = hb_par_amq_connection( 1 );

   if( conn )
      hb_retl( amqp_frames_enqueued( conn ) );
   else
      hb_errRT_BASE( EG_ARG, 3012, NULL, HB_ERR_FUNCNAME, HB_ERR_ARGS_BASEPARAMS );
}

/* amqp_data_in_buffer( pConn ) --> lValue */
HB_FUNC( AMQP_DATA_IN_BUFFER )
{
   amqp_connection_state_t conn = hb_par_amq_connection( 1 );

   if( conn )
      hb_retl( amqp_data_in_buffer( conn ) );
   else
      hb_errRT_BASE( EG_ARG, 3012, NULL, HB_ERR_FUNCNAME, HB_ERR_ARGS_BASEPARAMS );
}

/* amqp_tune_connection( pConn, nChannelMax, nFrameMax, nHeartbeatSecs ) --> nStatus */
HB_FUNC( AMQP_TUNE_CONNECTION )
{
   amqp_connection_state_t conn = hb_par_amq_connection( 1 );

   if( conn )
      hb_retni( amqp_tune_connection(
         conn /* state */,
         hb_parnidef( 2, 1 ) /* channel_max */,
         hb_parni( 3 ) /* frame_max */,
         hb_parni( 4 ) /* heartbeat */ ) );
   else
      hb_errRT_BASE( EG_ARG, 3012, NULL, HB_ERR_FUNCNAME, HB_ERR_ARGS_BASEPARAMS );
}

/* amqp_get_channel_max( pConn ) --> nChannelMax */
HB_FUNC( AMQP_GET_CHANNEL_MAX )
{
   amqp_connection_state_t conn = hb_par_amq_connection( 1 );

   if( conn )
      hb_retni( amqp_get_channel_max( conn ) );
   else
      hb_errRT_BASE( EG_ARG, 3012, NULL, HB_ERR_FUNCNAME, HB_ERR_ARGS_BASEPARAMS );
}

/* amqp_get_frame_max( pConn ) --> nFrameMax */
HB_FUNC( AMQP_GET_FRAME_MAX )
{
   amqp_connection_state_t conn = hb_par_amq_connection( 1 );

   if( conn )
#if HB_AMQP_VERS( 0, 6, 0 )
      hb_retni( amqp_get_frame_max( conn ) );
#else
      hb_retni( 0 );
#endif
   else
      hb_errRT_BASE( EG_ARG, 3012, NULL, HB_ERR_FUNCNAME, HB_ERR_ARGS_BASEPARAMS );
}

/* amqp_get_heartbeat( pConn ) --> nHeartbeatSecs */
HB_FUNC( AMQP_GET_HEARTBEAT )
{
   amqp_connection_state_t conn = hb_par_amq_connection( 1 );

   if( conn )
#if HB_AMQP_VERS( 0, 6, 0 )
      hb_retni( amqp_get_heartbeat( conn ) );
#else
      hb_retni( 0 );
#endif
   else
      hb_errRT_BASE( EG_ARG, 3012, NULL, HB_ERR_FUNCNAME, HB_ERR_ARGS_BASEPARAMS );
}

/* Wait for and consume a message
   amqp_consume_message( pConn, pEnvelope, nTimeoutMS ) --> nResponse */
HB_FUNC( AMQP_CONSUME_MESSAGE )
{
   amqp_connection_state_t conn = hb_par_amq_connection( 1 );
   amqp_envelope_t * envelope = hb_par_amq_envelope( 2 );

   if( conn && envelope )
   {
      struct timeval * timeout;

      if( HB_ISNUM( 3 ) )
      {
         timeout = ( struct timeval * ) hb_xgrabz( sizeof( struct timeval ) );
         timeout->tv_usec = hb_parni( 3 ) * 1000;  /* ms to us */
      }
      else
         timeout = NULL;  /* infinite */

      hb_retni( s_decode_reply(
         amqp_consume_message(
            conn,
            envelope,
            timeout,
            hb_parni( 4 ) /* flags */ ),
         "amqp_consume_message()" ) );

      if( timeout )
         hb_xfree( timeout );
   }
   else
      hb_errRT_BASE( EG_ARG, 3012, NULL, HB_ERR_FUNCNAME, HB_ERR_ARGS_BASEPARAMS );
}

HB_FUNC( AMQP_ENVELOPE_NEW )
{
   amqp_envelope_t ** ptr = ( amqp_envelope_t ** ) hb_gcAllocate( sizeof( amqp_envelope_t * ),
                                                                  &s_gc_amq_envelope_Funcs );

   *ptr = ( amqp_envelope_t * ) hb_xgrabz( sizeof( amqp_envelope_t ) );

   hb_retptrGC( ( void * ) ptr );
}

/* amqp_envelope_getmessagebody( pEnvelope ) --> cMessage */
HB_FUNC( AMQP_ENVELOPE_GETMESSAGEBODY )
{
   amqp_envelope_t * envelope = hb_par_amq_envelope( 1 );

   if( envelope )
      hb_retclen( envelope->message.body.bytes, envelope->message.body.len );
   else
      hb_errRT_BASE( EG_ARG, 3012, NULL, HB_ERR_FUNCNAME, HB_ERR_ARGS_BASEPARAMS );
}

/* amqp_envelope_getdeliverytag( pEnvelope ) --> nDeliveryTag */
HB_FUNC( AMQP_ENVELOPE_GETDELIVERYTAG )
{
   amqp_envelope_t * envelope = hb_par_amq_envelope( 1 );

   if( envelope )
      hb_retnint( envelope->delivery_tag );
   else
      hb_errRT_BASE( EG_ARG, 3012, NULL, HB_ERR_FUNCNAME, HB_ERR_ARGS_BASEPARAMS );
}

HB_FUNC( AMQP_ENVELOPE_GETROUTINGKEY )
{
   amqp_envelope_t * envelope = hb_par_amq_envelope( 1 );

   if( envelope )
      hb_retclen( envelope->routing_key.bytes, envelope->routing_key.len );
   else
      hb_errRT_BASE( EG_ARG, 3012, NULL, HB_ERR_FUNCNAME, HB_ERR_ARGS_BASEPARAMS );
}

HB_FUNC( AMQP_ENVELOPE_GETEXCHANGE )
{
   amqp_envelope_t * envelope = hb_par_amq_envelope( 1 );

   if( envelope )
      hb_retclen( envelope->exchange.bytes, envelope->exchange.len );
   else
      hb_errRT_BASE( EG_ARG, 3012, NULL, HB_ERR_FUNCNAME, HB_ERR_ARGS_BASEPARAMS );
}

/* amqp_maybe_release_buffers( pConn ) */
HB_FUNC( AMQP_MAYBE_RELEASE_BUFFERS )
{
   amqp_connection_state_t conn = hb_par_amq_connection( 1 );

   if( conn )
      amqp_maybe_release_buffers( conn );
   else
      hb_errRT_BASE( EG_ARG, 3012, NULL, HB_ERR_FUNCNAME, HB_ERR_ARGS_BASEPARAMS );
}

/* amqp_maybe_release_buffers_on_channel( pConn, nChannel ) */
HB_FUNC( AMQP_MAYBE_RELEASE_BUFFERS_ON_CHANNEL )
{
   amqp_connection_state_t conn = hb_par_amq_connection( 1 );

   if( conn )
      amqp_maybe_release_buffers_on_channel( conn, ( amqp_channel_t ) hb_parnidef( 2, 1 ) );
   else
      hb_errRT_BASE( EG_ARG, 3012, NULL, HB_ERR_FUNCNAME, HB_ERR_ARGS_BASEPARAMS );
}

/* amqp_error_string2( nStatus ) --> cStatus */
HB_FUNC( AMQP_ERROR_STRING2 )
{
   hb_retc_const( amqp_error_string2( hb_parni( 1 ) ) );
}

static PHB_ITEM s_ret_conn_info( struct amqp_connection_info * info )
{
   PHB_ITEM aInfo = hb_itemArrayNew( 6 );

   hb_arraySetC( aInfo, 1, info->user );
   hb_arraySetC( aInfo, 2, info->password );
   hb_arraySetC( aInfo, 3, info->host );
   hb_arraySetC( aInfo, 4, info->vhost );
   hb_arraySetNI( aInfo, 5, info->port );
   hb_arraySetL( aInfo, 6, ( HB_BOOL ) info->ssl );

   return aInfo;
}

/* amqp_default_connection_info() --> aInfo */
HB_FUNC( AMQP_DEFAULT_CONNECTION_INFO )
{
   struct amqp_connection_info info;

   amqp_default_connection_info( &info );

   hb_itemReturnRelease( s_ret_conn_info( &info ) );
}

/* amqp_parse_url( cURL, @aInfo ) --> nStatus */
HB_FUNC( AMQP_PARSE_URL )
{
   struct amqp_connection_info info;
   char * pszURL = hb_strdup( hb_parcx( 1 ) );

   memset( &info, 0, sizeof( info ) );

   hb_retni( amqp_parse_url( pszURL, &info ) );
   hb_itemParamStoreRelease( 2, s_ret_conn_info( &info ) );

   hb_xfree( pszURL );
}

/* amqp_version_number() --> nVersion */
HB_FUNC( AMQP_VERSION_NUMBER )
{
   hb_retnl( amqp_version_number() );
}

/* amqp_version() --> cVersion */
HB_FUNC( AMQP_VERSION )
{
   hb_retc( amqp_version() );
}