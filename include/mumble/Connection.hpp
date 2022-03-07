// Copyright 2022 The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_CONNECTION_HPP
#define MUMBLE_CONNECTION_HPP

#include "Cert.hpp"
#include "Key.hpp"
#include "Mumble.hpp"

#include <functional>

namespace mumble {
class Message;

class EXPORT Connection {
public:
	class P;
	using UniqueP = std::unique_ptr< P >;

	struct Feedback {
		std::function< void() > opened;
		std::function< void() > closed;

		std::function< void(Code code) > failed;

		std::function< uint32_t() > timeout;
		std::function< uint32_t() > timeouts;

		std::function< void(Message *message) > message;
	};

	Connection(Connection &&connection);
	Connection(const int32_t fd, const bool server);
	virtual ~Connection();

	virtual explicit operator bool() const;

	virtual Code operator()(
		const Feedback &feedback, const std::function< bool() > halt = []() { return false; });

	virtual const UniqueP &p() const;
	virtual int32_t fd() const;

	virtual Endpoint endpoint() const;
	virtual Endpoint peerEndpoint() const;

	virtual const Cert::Chain &cert() const;
	virtual Cert::Chain peerCert() const;

	virtual bool setCert(const Cert::Chain &cert, const Key &key);

	virtual Code process(
		const bool wait = true, const std::function< bool() > halt = []() { return false; });
	virtual Code write(
		const Message &message, const bool wait = true, const std::function< bool() > halt = []() { return false; });

private:
	Connection(const Connection &) = delete;
	virtual Connection &operator=(const Connection &) = delete;

	UniqueP m_p;
};
} // namespace mumble

#endif