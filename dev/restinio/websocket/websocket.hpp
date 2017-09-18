/*
	restinio
*/

/*!
	WebSocket messgage handler definition.
*/

#pragma once

#include <functional>

#include <restinio/websocket/ws_connection_handle.hpp>
#include <restinio/websocket/ws_message.hpp>
#include <restinio/websocket/impl/ws_connection.hpp>
#include <restinio/utils/base64.hpp>
#include <restinio/utils/sha1.hpp>

namespace restinio
{

namespace websocket
{

const std::string websocket_accept_field_suffix{
	"258EAFA5-E914-47DA-95CA-C5AB0DC85B11"};

//
// ws_t
//

//! A WebSocket bind.
class ws_t
{
	public:
		ws_t( const ws_t & ) = delete;
		ws_t( ws_t && ) = delete;
		void operator = ( const ws_t & ) = delete;
		void operator = ( ws_t && ) = delete;

		ws_t(
			ws_connection_handle_t ws_connection_handle )
			:	m_ws_connection_handle{ std::move( ws_connection_handle ) }
		{}

		~ws_t()
		{
			try
			{
				close();
			}
			catch( ... )
			{}
		}

		void
		close()
		{
			if( m_ws_connection_handle )
			{
				auto con = std::move( m_ws_connection_handle );
				con->close();
			}
		}

		//! Send_websocket message
		void
		send_message(
			bool final,
			opcode_t opcode,
			buffer_storage_t payload )
		{
			if( m_ws_connection_handle )
			{
				buffers_container_t bufs;
				bufs.reserve( 2 );

				// Create header serialize it and append to bufs .
				impl::ws_message_details_t details{
					final, opcode, asio::buffer_size( payload.buf() ) };

				bufs.emplace_back(
					impl::write_message_details( details ) );

				bufs.emplace_back( std::move( payload ) );

				m_ws_connection_handle->write_data( std::move( bufs ) );
			}
			else
			{
				throw exception_t{ "websocket is closed" };
			}
		}

		void
		send_message( const ws_message_t & msg )
		{
			send_message(
				msg.header().m_is_final,
				msg.header().m_opcode,
				buffer_storage_t( msg.payload() ) );
		}

	private:
		ws_connection_handle_t m_ws_connection_handle;
};

//! Alias for ws_t handle.
using ws_handle_t = std::shared_ptr< ws_t >;
using ws_weak_handle_t = std::weak_ptr< ws_t >;

//
// upgrade
//

template <
		typename TRAITS,
		typename WS_MESSAGE_HANDLER,
		typename WS_CLOSE_HANDLER >
ws_handle_t
upgrade(
	request_t & req,
	http_header_fields_t upgrade_response_header_fields,
	WS_MESSAGE_HANDLER ws_message_handler,
	WS_CLOSE_HANDLER ws_close_handler )
{
	// TODO: check if upgrade request.

	//! Check if mandatory field is available.
	if( !upgrade_response_header_fields.has_field( http_field::sec_websocket_accept ) )
	{
		throw exception_t{
			fmt::format( "{} field is mandatory for upgrade response",
				field_to_string( http_field::sec_websocket_accept ) ) };
	}

	if( !upgrade_response_header_fields.has_field( http_field::upgrade ) )
	{
		upgrade_response_header_fields.set_field( http_field::upgrade, "websocket" );
	}

	using connection_t = restinio::impl::connection_t< TRAITS >;
	auto conn_ptr = std::move( restinio::impl::access_req_connection( req ) );
	if( !conn_ptr )
	{
		throw exception_t{ "no connection for upgrade: already moved" };
	}
	auto & con = dynamic_cast< connection_t & >( *conn_ptr );

	using ws_connection_t = restinio::websocket::impl::ws_connection_t< TRAITS, WS_MESSAGE_HANDLER, WS_CLOSE_HANDLER >;

	auto upgrade_internals = con.move_upgrade_internals();
	auto ws_connection =
		std::make_shared< ws_connection_t >(
			con.connection_id(),
			std::move( upgrade_internals.m_socket ),
			std::move( upgrade_internals.m_strand ),
			std::move( upgrade_internals.m_timer_guard ),
			con.get_settings(),
			std::move( ws_message_handler ),
			std::move( ws_close_handler ) );

	buffers_container_t upgrade_response_bufs;
	{
		http_response_header_t upgrade_response_header{ 101, "Switching Protocols" };
		upgrade_response_header.swap_fields( upgrade_response_header_fields );
		upgrade_response_header.connection( http_connection_header_t::upgrade );

		const auto content_length_flag =
			restinio::impl::content_length_field_presence_t::skip_content_length;

		upgrade_response_bufs.emplace_back(
			restinio::impl::create_header_string(
				upgrade_response_header,
				content_length_flag ) );
	}
	ws_connection->write_data( std::move( upgrade_response_bufs ) );

	auto result = std::make_shared< ws_t >( ws_connection );

	// Now we a ready to receive messages.
	ws_connection->init_read(
		// Makes a weak handle, and stores it in ws_connection.
		result );

	// Returns strong handle on websocket, thus giving an ownership.
	return result;
}

//
// upgrade
//

template <
		typename TRAITS,
		typename WS_MESSAGE_HANDLER,
		typename WS_CLOSE_HANDLER >
auto
upgrade(
	request_t & req,
	std::string sec_websocket_accept_field_value,
	WS_MESSAGE_HANDLER ws_message_handler,
	WS_CLOSE_HANDLER ws_close_handler )
{
	http_header_fields_t upgrade_response_header_fields;
	upgrade_response_header_fields.set_field(
		http_field::sec_websocket_accept,
		std::move( sec_websocket_accept_field_value ) );

	return
		upgrade< TRAITS, WS_MESSAGE_HANDLER, WS_CLOSE_HANDLER >(
			req,
			std::move( upgrade_response_header_fields ),
			std::move( ws_message_handler ),
			std::move( ws_close_handler ) );
}

//
// upgrade
//

template <
		typename TRAITS,
		typename WS_MESSAGE_HANDLER,
		typename WS_CLOSE_HANDLER >
auto
upgrade(
	request_t & req,
	std::string sec_websocket_accept_field_value,
	std::string sec_websocket_protocol_field_value,
	WS_MESSAGE_HANDLER ws_message_handler,
	WS_CLOSE_HANDLER ws_close_handler )
{
	http_header_fields_t upgrade_response_header_fields;
	upgrade_response_header_fields.set_field(
		http_field::sec_websocket_accept,
		std::move( sec_websocket_accept_field_value ) );

	upgrade_response_header_fields.set_field(
		http_field::sec_websocket_protocol,
		std::move( sec_websocket_protocol_field_value ) );

	return
		upgrade< TRAITS, WS_MESSAGE_HANDLER, WS_CLOSE_HANDLER >(
			req,
			std::move( upgrade_response_header_fields ),
			std::move( ws_message_handler ),
			std::move( ws_close_handler ) );
}

//
// upgrade
//

template <
		typename TRAITS,
		typename WS_MESSAGE_HANDLER,
		typename WS_CLOSE_HANDLER >
auto
upgrade(
	request_t & req,
	WS_MESSAGE_HANDLER ws_message_handler,
	WS_CLOSE_HANDLER ws_close_handler )
{
	auto ws_key = req.header().get_field( restinio::http_field::sec_websocket_key );

	ws_key.append( websocket_accept_field_suffix );

	auto digest = restinio::utils::sha1::make_digest( ws_key );

	std::string sec_websocket_accept_field_value = utils::base64::encode(
		utils::sha1::to_string( digest ) );

	http_header_fields_t upgrade_response_header_fields;
	upgrade_response_header_fields.set_field(
		http_field::sec_websocket_accept,
		std::move( sec_websocket_accept_field_value ) );

	return
		upgrade< TRAITS, WS_MESSAGE_HANDLER, WS_CLOSE_HANDLER >(
			req,
			std::move( upgrade_response_header_fields ),
			std::move( ws_message_handler ),
			std::move( ws_close_handler ) );
}

} /* namespace websocket */

} /* namespace restinio */
