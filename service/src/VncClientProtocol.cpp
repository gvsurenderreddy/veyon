/*
 * VncClientProtocol.cpp - implementation of the VncClientProtocol class
 *
 * Copyright (c) 2017 Tobias Doerffel <tobydox/at/users/dot/sf/dot/net>
 *
 * This file is part of iTALC - http://italc.sourceforge.net
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 */

#include "ItalcCore.h"

#ifdef ITALC_BUILD_WIN32
#include <winsock2.h>
#endif

#include <QTcpSocket>

extern "C"
{
#include "rfb/rfbproto.h"
}

#include "VncClientProtocol.h"
#include "VariantStream.h"



VncClientProtocol::VncClientProtocol( QTcpSocket* socket, const QString& vncPassword ) :
	m_socket( socket ),
	m_state( Disconnected ),
	m_vncPassword( vncPassword.toLatin1() )
{
}


void VncClientProtocol::start()
{
	m_state = Protocol;
}



bool VncClientProtocol::read()
{
	switch( m_state )
	{
	case Protocol:
		return readProtocol();

	case SecurityInit:
		return receiveSecurityTypes();

	case SecurityChallenge:
		return receiveSecurityChallenge();

	case SecurityResult:
		return receiveSecurityResult();

	default:
		break;
	}

	return false;
}



bool VncClientProtocol::readProtocol()
{
	if( m_socket->bytesAvailable() == sz_rfbProtocolVersionMsg )
	{
		char protocol[sz_rfbProtocolVersionMsg+1];
		m_socket->read( protocol, sz_rfbProtocolVersionMsg );
		protocol[sz_rfbProtocolVersionMsg] = 0;

		int protocolMajor = 0, protocolMinor = 0;

		if( sscanf( protocol, rfbProtocolVersionFormat, &protocolMajor, &protocolMinor ) != 2 ||
				protocolMajor != 3 || protocolMinor < 7 )
		{
			qCritical( "VncClientProtocol:::readProtocol(): protocol initialization failed" );
			m_socket->close();

			return false;
		}

		m_socket->write( protocol, sz_rfbProtocolVersionMsg );

		m_state = SecurityInit;

		return true;
	}

	return false;
}



bool VncClientProtocol::receiveSecurityTypes()
{
	if( m_socket->bytesAvailable() >= 2 )
	{
		char securityTypeCount = 0;

		m_socket->read( &securityTypeCount, sizeof(securityTypeCount) );

		if( securityTypeCount < 1 || securityTypeCount > MaxSecurityTypes )
		{
			qCritical( "VncClientProtocol::receiveSecurityTypes(): invalid number of security types received!" );
			m_socket->close();

			return false;
		}

		QByteArray securityTypeList = m_socket->read( securityTypeCount );
		if( securityTypeList.count() != securityTypeCount )
		{
			qCritical( "VncClientProtocol::receiveSecurityTypes(): could not read security types!" );
			m_socket->close();

			return false;
		}

		char securityType = rfbSecTypeVncAuth;

		if( securityTypeList.contains( securityType ) == false )
		{
			qCritical( "VncClientProtocol::receiveSecurityTypes(): no supported security type!" );
			m_socket->close();

			return false;
		}

		m_socket->write( &securityType, sizeof(securityType) );

		m_state = SecurityChallenge;

		return true;
	}

	return false;
}



bool VncClientProtocol::receiveSecurityChallenge()
{
	if( m_socket->bytesAvailable() >= CHALLENGESIZE )
	{
		uint8_t challenge[CHALLENGESIZE];
		m_socket->read( (char *) challenge, CHALLENGESIZE );

		rfbEncryptBytes( challenge, m_vncPassword.data() );

		m_socket->write( (const char *) challenge, CHALLENGESIZE );

		m_state = SecurityResult;

		return true;
	}

	return false;
}



bool VncClientProtocol::receiveSecurityResult()
{
	if( m_socket->bytesAvailable() >= 4 )
	{
		uint32_t authResult = 0;

		m_socket->read( (char *) &authResult, sizeof(authResult) );

		if( ntohl( authResult ) != rfbVncAuthOK )
		{
			qCritical( "VncClientProtocol::receiveSecurityResult(): authentication failed!" );
			m_socket->close();
			return false;
		}

		qDebug( "VncClientProtocol::receiveSecurityResult(): authentication successful" );

		m_state = Authenticated;
		return true;
	}

	return false;
}