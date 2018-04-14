#include "connection.h"
#include <stdio.h>
#include <string.h>

#pragma region
struct sockaddr_un {
	unsigned short sun_family;               /* AF_UNIX */
	char           sun_path[108];            /* pathname */
};
struct sockaddr {
	unsigned short sa_family;   // address family, AF_xxx
	char           sa_data[14]; // 14 bytes of protocol address
};
#define AF_UNIX     0x0001
#define SOCK_STREAM 0x0001
#define F_SETFL     0x0004
#define O_RDONLY    0x0000
#define O_WRONLY    0x0001
#define O_CREAT     0x0200
#define O_APPEND    0x0008
#define O_NONBLOCK  0x0004
#pragma endregion wine-specific header thingy
#pragma region
_declspec(naked) void __syscall() {
	__asm {
		int 0x80
		jnc noerror
		neg eax
		noerror :
		ret
	}
}
_declspec(naked) unsigned int __cdecl getpid() {
	__asm {
		mov eax, 0x14
		jmp __syscall
		ret
	}
}
_declspec(naked) int __cdecl close(int fd) {
	__asm {
		mov eax, 0x06
		jmp __syscall
		ret
	}
}
_declspec(naked) int __cdecl fcntl(unsigned int fd, unsigned int cmd, unsigned long arg) {
	__asm {
		mov eax, 0x5c
		jmp __syscall
		ret
	}
}
_declspec(naked) int __cdecl open(const char* filename, int flags, int mode) {
	__asm {
		mov eax, 0x05
		jmp __syscall
		ret
	}
}
_declspec(naked) int __cdecl write(unsigned int fd, const char* buf, unsigned int count) {
	__asm {
		mov eax, 0x04
		jmp __syscall
		ret
	}
}
_declspec(naked) int __cdecl read(unsigned int fd, char* buf, unsigned int count) {
	__asm {
		mov eax, 0x03
		jmp __syscall
		ret
	}
}
_declspec(naked) int __cdecl socket(int domain, int type, int protocol) {
	__asm {
		mov eax, 0x61
		jmp __syscall
		ret
	}
}
_declspec(naked) int __cdecl connect(int sockfd, const struct sockaddr *addr, unsigned int addrlen) {
	__asm {
		mov eax, 0x62
		jmp __syscall
		ret
	}
}
_declspec(naked) int __cdecl send(int sockfd, const void* buf, unsigned int len, int flags, int a, int b) {
	__asm {
		mov eax, 0x85
		jmp __syscall
		ret
	}
}
_declspec(naked) int __cdecl recv(int fd, void* buf, unsigned int len, int flags, int a, int b) {
	__asm {
		mov eax, 0x1d
		jmp __syscall
		ret
	}
}
#pragma endregion syscall wrappers

int GetProcessId()
{
	return (int)getpid();
}

struct BaseConnectionUnix : public BaseConnection {
	int sock{ -1 };
};

static BaseConnectionUnix Connection;
static sockaddr_un PipeAddr{};
#ifdef MSG_NOSIGNAL
static int MsgFlags = MSG_NOSIGNAL;
#else
static int MsgFlags = 0;
#endif

static const char* GetTempPath()
{
	const char* temp = getenv("TMPDIR");
	temp = temp ? temp : "/tmp";
	return temp;
}

/*static*/ BaseConnection* BaseConnection::Create()
{
	PipeAddr.sun_family = AF_UNIX;
	return &Connection;
}

/*static*/ void BaseConnection::Destroy(BaseConnection*& c)
{
	auto self = reinterpret_cast<BaseConnectionUnix*>(c);
	self->Close();
	c = nullptr;
}

bool BaseConnection::Open()
{
	const char* tempPath = GetTempPath();
	auto self = reinterpret_cast<BaseConnectionUnix*>(this);
	self->sock = socket(AF_UNIX, SOCK_STREAM, 0);
	if (self->sock == -1) {
		return false;
	}

	int fcres = fcntl((unsigned int)self->sock, (unsigned int)F_SETFL, (unsigned int)O_NONBLOCK);
	if (fcres < 0) {
		return false;
	}

	for (int pipeNum = 0; pipeNum < 10; ++pipeNum) {
		snprintf(
			PipeAddr.sun_path, sizeof(PipeAddr.sun_path), "%s/discord-ipc-%d", tempPath, pipeNum);
		int err = connect(self->sock, (const sockaddr*)&PipeAddr, sizeof(PipeAddr));
		if (err == 0) {
			self->isOpen = true;
			return true;
		}
	}
	self->Close();
	return false;
}

bool BaseConnection::Close()
{
	auto self = reinterpret_cast<BaseConnectionUnix*>(this);
	if (self->sock == -1) {
		return false;
	}
	close(self->sock);
	self->sock = -1;
	self->isOpen = false;
	return true;
}

bool BaseConnection::Write(const void* data, unsigned int length)
{
	auto self = reinterpret_cast<BaseConnectionUnix*>(this);

	if (self->sock == -1) {
		return false;
	}

	int sentBytes = send(self->sock, data, length, MsgFlags, 0, 0);
	if (sentBytes < 0) {
		Close();
	}
	return sentBytes == (int)length;
}

bool BaseConnection::Read(void* data, unsigned int length)
{
	auto self = reinterpret_cast<BaseConnectionUnix*>(this);

	if (self->sock == -1) {
		return false;
	}

	int res = (int)recv(self->sock, data, length, MsgFlags, 0, 0);
	if (res < 0) {
		if (res == -35) { // EAGAIN
			return false;
		}
		Close();
	}
	return res == (int)length;
}