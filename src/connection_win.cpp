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
#define AF_UNIX		1
#define SOCK_STREAM		1
#define F_SETFL 4
#define O_RDONLY    00000000
#define O_WRONLY	00000001
#define O_CREAT     00000100
#define O_APPEND	00002000
#define O_NONBLOCK  00004000
#pragma endregion wine-specific header thingy
#pragma region
_declspec(naked) unsigned int getpid() {
	__asm {
		mov eax, 0x14
		int 0x80
		ret
	}
}
_declspec(naked) int close(int fd) {
	__asm {
		push ebx

		mov eax, 0x06
		mov ebx, [esp + 4 + 4]
		int 0x80

		pop ebx
		ret
	}
}
_declspec(naked) int socketcall(int call, void* args) {
	__asm {
		push ebx

		mov eax, 0x66
		mov ebx, [esp + 4 + 4]
		mov ecx, [esp + 4 + 8]
		int 0x80

		pop ebx
		ret
	}
}
_declspec(naked) int fcntl(unsigned int fd, unsigned int cmd, unsigned long arg) {
	__asm {
		push ebx

		mov eax, 0x37
		mov ebx, [esp + 4 + 4]
		mov ecx, [esp + 4 + 8]
		mov edx, [esp + 4 + 12]
		int 0x80

		pop ebx
		ret
	}
}
_declspec(naked) int open(const char* filename, int flags, int mode) {
	__asm {
		push ebx

		mov eax, 0x05
		mov ebx, [esp + 4 + 4]
		mov ecx, [esp + 4 + 8]
		mov edx, [esp + 4 + 12]
		int 0x80

		pop ebx
		ret
	}
}
_declspec(naked) int write(unsigned int fd, const char* buf, unsigned int count) {
	__asm {
		push ebx

		mov eax, 0x04
		mov ebx, [esp + 4 + 4]
		mov ecx, [esp + 4 + 8]
		mov edx, [esp + 4 + 12]
		int 0x80

		pop ebx
		ret
	}
}
_declspec(naked) int read(unsigned int fd, char* buf, unsigned int count) {
	__asm {
		push ebx

		mov eax, 0x03
		mov ebx, [esp + 4 + 4]
		mov ecx, [esp + 4 + 8]
		mov edx, [esp + 4 + 12]
		int 0x80

		pop ebx
		ret
	}
}
#pragma endregion syscall wrappers
#pragma region
int socket(int domain, int type, int protocol) {
	void* args[3];
	args[0] = (void*)(int*)domain;
	args[1] = (void*)(int*)type;
	args[2] = (void*)(int*)protocol;
	return socketcall(1, args);
}
int connect(int sockfd, const struct sockaddr *addr, unsigned int addrlen) {
	void* args[3];
	args[0] = (void*)(int*)sockfd;
	args[1] = (void*)addr;
	args[2] = (void*)(int*)addrlen;
	return socketcall(3, args);
}
int send(int sockfd, const void* buf, unsigned int len, int flags) {
	void* args[4];
	args[0] = (void*)(int*)sockfd;
	args[1] = (void*)buf;
	args[2] = (void*)(unsigned int*)len;
	args[3] = (void*)(int*)flags;
	return socketcall(9, args);
}
int recv(int fd, void* buf, unsigned int len, int flags) {
	void* args[4];
	args[0] = (void*)(int*)fd;
	args[1] = (void*)buf;
	args[2] = (void*)(unsigned int*)len;
	args[3] = (void*)(int*)flags;
	return socketcall(10, args);
}
#pragma endregion socketcall wrappers

char* getenv_(char* name) // written by https://github.com/Francesco149
{
	static char buf[1024 * 1024];

	int fd, n;
	static char* end = 0;
	int namelen;
	char* p;

	if (!end) {
		fd = open("/proc/self/environ", 0, 0);
		if (fd < 0) {
			return 0;
		}

		n = read(fd, buf, sizeof(buf));
		if (n < 0) {
			return 0;
		}

		close(fd);
		end = buf + n;
	}

	namelen = strlen(name);

	for (p = buf; p < end;) {
		if (!strncmp(p, name, namelen)) {
			return p + namelen + 1; /* skip name and the = */
		}

		for (; *p && p < end; ++p); /* skip to next entry */
		++p;
	}

	return 0;
}

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
	const char* temp = getenv_("XDG_RUNTIME_DIR");
	temp = temp ? temp : getenv_("TMPDIR");
	temp = temp ? temp : getenv_("TMP");
	temp = temp ? temp : getenv_("TEMP");
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

	int sentBytes = send(self->sock, data, length, MsgFlags);
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

	int res = (int)recv(self->sock, data, length, MsgFlags);
	if (res < 0) {
		if (res == -11) { // EAGAIN
			return false;
		}
		Close();
	}
	return res == (int)length;
}