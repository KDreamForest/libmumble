// Copyright 2022 The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "Connection.hpp"

#include "Pack.hpp"

#include "mumble/Message.hpp"

#include <cstddef>
#include <utility>

#include <boost/thread/interruption.hpp>
#include <boost/thread/thread_only.hpp>

using namespace mumble;

Connection::Connection(SocketTLS &&socket) : SocketTLS(std::move(socket)) {
	setBlocking(false);
}

Connection::~Connection() {
	stop();
}

void Connection::start(const Feedback &feedback) {
	m_feedback = feedback;

	m_timeouts = 0;

	m_thread = std::make_unique< boost::thread >(&Connection::thread, this);
}

void Connection::stop() {
	m_thread->interrupt();

	trigger();

	if (m_thread->joinable()) {
		m_thread->join();
	}
}

mumble::Code Connection::handleCode(const Code code) {
	using Code = mumble::Code;
	using TLS  = SocketTLS::Code;

	switch (code) {
		case TLS::Memory:
			m_feedback.failed(Code::Memory);
			return Code::Memory;
		case TLS::Failure:
			m_feedback.failed(Code::Failure);
			return Code::Failure;
		case TLS::Unknown:
			break;
		case TLS::Success:
			m_timeouts = 0;
			return Code::Success;
		case TLS::Retry:
			return Code::Retry;
		case TLS::Shutdown:
			m_feedback.closed();
			return Code::Disconnect;
		case TLS::WaitIn:
		case TLS::WaitOut: {
			const auto state = wait(code == TLS::WaitIn, code == TLS::WaitOut, m_feedback.timeout());
			return handleState(state);
		}
	}

	return Code::Unknown;
}

mumble::Code Connection::handleState(const State state) {
	using Code = mumble::Code;

	if (state & Socket::InReady || state & Socket::OutReady) {
		return Code::Retry;
	}

	if (state & Socket::Triggered) {
		return Code::Retry;
	}

	if (state & Socket::Timeout) {
		if (++m_timeouts < m_feedback.timeouts()) {
			return Code::Retry;
		}

		m_feedback.failed(Code::Timeout);
		return Code::Timeout;
	}

	if (state & Socket::Disconnected) {
		m_feedback.closed();
		return Code::Disconnect;
	}

	if (state & Socket::Error) {
		m_feedback.failed(Code::Failure);
		return Code::Failure;
	}

	return Code::Unknown;
}

mumble::Code Connection::read(BufRef buf) {
	using Code = mumble::Code;

	while (!m_thread->interruption_requested()) {
		const auto code = handleCode(SocketTLS::read(buf));
		if (code == Code::Retry) {
			continue;
		} else {
			return code;
		}
	}

	return Code::Cancel;
}

mumble::Code Connection::write(BufRefConst buf, const std::atomic_bool &halt) {
	using Code = mumble::Code;

	while (!halt && !m_thread->interruption_requested()) {
		const auto code = handleCode(SocketTLS::write(buf));
		if (code == Code::Retry) {
			continue;
		} else {
			return code;
		}
	}

	return Code::Cancel;
}

void Connection::thread() {
	using Code       = mumble::Code;
	namespace Thread = boost::this_thread;

	while (!Thread::interruption_requested()) {
		const auto code = handleCode(m_server ? accept() : connect());
		if (code == Code::Success) {
			break;
		}

		if (code == Code::Retry) {
			continue;
		}

		return;
	}

	if (Thread::interruption_requested()) {
		return;
	}

	m_feedback.opened();

	auto state = Socket::InReady;

	while (!Thread::interruption_requested()) {
		switch (handleState(state)) {
			case Code::Success:
			case Code::Retry:
				do {
					Pack::NetHeader header;
					if (read({ reinterpret_cast< std::byte * >(&header), sizeof(header) }) != Code::Success) {
						return;
					}

					Pack pack(header);
					if (pack.type() == Message::Type::Unknown) {
						m_feedback.failed(Code::Invalid);
						return;
					}

					if (read(pack.data()) != Code::Success) {
						return;
					}

					m_feedback.message(pack);
				} while (pending() >= sizeof(Pack::NetHeader));

				break;
			default:
				return;
		}

		state = wait(true, false, m_feedback.timeout());
	}

	disconnect();

	m_feedback.closed();
}
