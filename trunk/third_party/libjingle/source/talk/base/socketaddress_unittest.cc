/*
 * libjingle
 * Copyright 2004--2011, Google Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  1. Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright notice,
 *     this list of conditions and the following disclaimer in the documentation
 *     and/or other materials provided with the distribution.
 *  3. The name of the author may not be used to endorse or promote products
 *     derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
 * EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifdef POSIX
#include <netinet/in.h>  // for sockaddr_in
#endif

#include "talk/base/gunit.h"
#include "talk/base/socketaddress.h"
#include "talk/base/ipaddress.h"

namespace talk_base {

const in6_addr kTestV6Addr =  { { {0x20, 0x01, 0x0d, 0xb8,
                                   0x10, 0x20, 0x30, 0x40,
                                   0x50, 0x60, 0x70, 0x80,
                                   0x90, 0xA0, 0xB0, 0xC0} } };
const in6_addr kMappedV4Addr = { { {0x00, 0x00, 0x00, 0x00,
                                    0x00, 0x00, 0x00, 0x00,
                                    0x00, 0x00, 0xFF, 0xFF,
                                    0x01, 0x02, 0x03, 0x04} } };
const std::string kTestV6AddrString = "2001:db8:1020:3040:5060:7080:90a0:b0c0";
const std::string kTestV6AddrFullString =
    "[2001:db8:1020:3040:5060:7080:90a0:b0c0]:5678";

TEST(SocketAddressTest, TestDefaultCtor) {
  SocketAddress addr;
  EXPECT_FALSE(addr.IsUnresolvedIP());
  EXPECT_EQ(IPAddress(INADDR_ANY), addr.ipaddr());
  EXPECT_EQ(0, addr.port());
  EXPECT_EQ("", addr.hostname());
  EXPECT_EQ("0.0.0.0:0", addr.ToString());
}

TEST(SocketAddressTest, TestIPPortCtor) {
  SocketAddress addr(IPAddress(0x01020304), 5678);
  EXPECT_FALSE(addr.IsUnresolvedIP());
  EXPECT_EQ(IPAddress(0x01020304U), addr.ipaddr());
  EXPECT_EQ(5678, addr.port());
  EXPECT_EQ("", addr.hostname());
  EXPECT_EQ("1.2.3.4:5678", addr.ToString());
}

TEST(SocketAddressTest, TestIPv4StringPortCtor) {
  SocketAddress addr("1.2.3.4", 5678);
  EXPECT_FALSE(addr.IsUnresolvedIP());
  EXPECT_EQ(IPAddress(0x01020304U), addr.ipaddr());
  EXPECT_EQ(5678, addr.port());
  EXPECT_EQ("1.2.3.4", addr.hostname());
  EXPECT_EQ("1.2.3.4:5678", addr.ToString());
}

TEST(SocketAddressTest, TestIPv6StringPortCtor) {
  SocketAddress addr2(kTestV6AddrString, 1234);
  IPAddress tocheck(kTestV6Addr);

  EXPECT_FALSE(addr2.IsUnresolvedIP());
  EXPECT_EQ(tocheck, addr2.ipaddr());
  EXPECT_EQ(1234, addr2.port());
  EXPECT_EQ(kTestV6AddrString, addr2.hostname());
  EXPECT_EQ("[" + kTestV6AddrString + "]:1234", addr2.ToString());
}

TEST(SocketAddressTest, TestSpecialStringPortCtor) {
  // inet_addr doesn't handle this address properly.
  SocketAddress addr("255.255.255.255", 5678);
  EXPECT_FALSE(addr.IsUnresolvedIP());
  EXPECT_EQ(IPAddress(0xFFFFFFFFU), addr.ipaddr());
  EXPECT_EQ(5678, addr.port());
  EXPECT_EQ("255.255.255.255", addr.hostname());
  EXPECT_EQ("255.255.255.255:5678", addr.ToString());
}

TEST(SocketAddressTest, TestHostnamePortCtor) {
  SocketAddress addr("a.b.com", 5678);
  EXPECT_TRUE(addr.IsUnresolvedIP());
  EXPECT_EQ(IPAddress(INADDR_ANY), addr.ipaddr());
  EXPECT_EQ(5678, addr.port());
  EXPECT_EQ("a.b.com", addr.hostname());
  EXPECT_EQ("a.b.com:5678", addr.ToString());
}

TEST(SocketAddressTest, TestCopyCtor) {
  SocketAddress from("1.2.3.4", 5678);
  SocketAddress addr(from);
  EXPECT_FALSE(addr.IsUnresolvedIP());
  EXPECT_EQ(IPAddress(0x01020304U), addr.ipaddr());
  EXPECT_EQ(5678, addr.port());
  EXPECT_EQ("1.2.3.4", addr.hostname());
  EXPECT_EQ("1.2.3.4:5678", addr.ToString());
}

TEST(SocketAddressTest, TestAssign) {
  SocketAddress from("1.2.3.4", 5678);
  SocketAddress addr(IPAddress(0x88888888), 9999);
  addr = from;
  EXPECT_FALSE(addr.IsUnresolvedIP());
  EXPECT_EQ(IPAddress(0x01020304U), addr.ipaddr());
  EXPECT_EQ(5678, addr.port());
  EXPECT_EQ("1.2.3.4", addr.hostname());
  EXPECT_EQ("1.2.3.4:5678", addr.ToString());
}

TEST(SocketAddressTest, TestSetIPPort) {
  SocketAddress addr(IPAddress(0x88888888), 9999);
  addr.SetIP(IPAddress(0x01020304));
  addr.SetPort(5678);
  EXPECT_FALSE(addr.IsUnresolvedIP());
  EXPECT_EQ(IPAddress(0x01020304U), addr.ipaddr());
  EXPECT_EQ(5678, addr.port());
  EXPECT_EQ("", addr.hostname());
  EXPECT_EQ("1.2.3.4:5678", addr.ToString());
}

TEST(SocketAddressTest, TestSetIPFromString) {
  SocketAddress addr(IPAddress(0x88888888), 9999);
  addr.SetIP("1.2.3.4");
  addr.SetPort(5678);
  EXPECT_FALSE(addr.IsUnresolvedIP());
  EXPECT_EQ(IPAddress(0x01020304U), addr.ipaddr());
  EXPECT_EQ(5678, addr.port());
  EXPECT_EQ("1.2.3.4", addr.hostname());
  EXPECT_EQ("1.2.3.4:5678", addr.ToString());
}

TEST(SocketAddressTest, TestSetIPFromHostname) {
  SocketAddress addr(IPAddress(0x88888888), 9999);
  addr.SetIP("a.b.com");
  addr.SetPort(5678);
  EXPECT_TRUE(addr.IsUnresolvedIP());
  EXPECT_EQ(IPAddress(INADDR_ANY), addr.ipaddr());
  EXPECT_EQ(5678, addr.port());
  EXPECT_EQ("a.b.com", addr.hostname());
  EXPECT_EQ("a.b.com:5678", addr.ToString());
  addr.SetResolvedIP(IPAddress(0x01020304));
  EXPECT_FALSE(addr.IsUnresolvedIP());
  EXPECT_EQ(IPAddress(0x01020304U), addr.ipaddr());
  EXPECT_EQ("a.b.com", addr.hostname());
  EXPECT_EQ("a.b.com:5678", addr.ToString());
}

TEST(SocketAddressTest, TestFromIPv4String) {
  SocketAddress addr;
  EXPECT_TRUE(addr.FromString("1.2.3.4:5678"));
  EXPECT_FALSE(addr.IsUnresolvedIP());
  EXPECT_EQ(IPAddress(0x01020304U), addr.ipaddr());
  EXPECT_EQ(5678, addr.port());
  EXPECT_EQ("1.2.3.4", addr.hostname());
  EXPECT_EQ("1.2.3.4:5678", addr.ToString());
}

TEST(SocketAddressTest, TestFromIPv6String) {
  SocketAddress addr;
  EXPECT_TRUE(addr.FromString(kTestV6AddrFullString));
  EXPECT_FALSE(addr.IsUnresolvedIP());
  EXPECT_EQ(5678, addr.port());
  EXPECT_EQ(kTestV6AddrString, addr.hostname());
  EXPECT_EQ(kTestV6AddrFullString, addr.ToString());
}

TEST(SocketAddressTest, TestFromHostname) {
  SocketAddress addr;
  EXPECT_TRUE(addr.FromString("a.b.com:5678"));
  EXPECT_TRUE(addr.IsUnresolvedIP());
  EXPECT_EQ(IPAddress(INADDR_ANY), addr.ipaddr());
  EXPECT_EQ(5678, addr.port());
  EXPECT_EQ("a.b.com", addr.hostname());
  EXPECT_EQ("a.b.com:5678", addr.ToString());
}

TEST(SocketAddressTest, TestToFromSockAddr) {
  SocketAddress from("1.2.3.4", 5678), addr;
  sockaddr_in addr_in;
  from.ToSockAddr(&addr_in);
  EXPECT_TRUE(addr.FromSockAddr(addr_in));
  EXPECT_FALSE(addr.IsUnresolvedIP());
  EXPECT_EQ(IPAddress(0x01020304U), addr.ipaddr());
  EXPECT_EQ(5678, addr.port());
  EXPECT_EQ("", addr.hostname());
  EXPECT_EQ("1.2.3.4:5678", addr.ToString());
}

TEST(SocketAddressTest, TestToFromSockAddrStorage) {
  SocketAddress from("1.2.3.4", 5678), addr;
  sockaddr_storage addr_storage;
  from.ToSockAddrStorage(&addr_storage);
  EXPECT_TRUE(SocketAddressFromSockAddrStorage(addr_storage, &addr));
  EXPECT_FALSE(addr.IsUnresolvedIP());
  EXPECT_EQ(IPAddress(0x01020304U), addr.ipaddr());
  EXPECT_EQ(5678, addr.port());
  EXPECT_EQ("", addr.hostname());
  EXPECT_EQ("1.2.3.4:5678", addr.ToString());

  addr.Clear();
  from.ToDualStackSockAddrStorage(&addr_storage);
  EXPECT_TRUE(SocketAddressFromSockAddrStorage(addr_storage, &addr));
  EXPECT_FALSE(addr.IsUnresolvedIP());
  EXPECT_EQ(IPAddress(kMappedV4Addr), addr.ipaddr());
  EXPECT_EQ(5678, addr.port());
  EXPECT_EQ("", addr.hostname());
  EXPECT_EQ("[::ffff:1.2.3.4]:5678", addr.ToString());

  addr = from;
  addr_storage.ss_family = AF_UNSPEC;
  EXPECT_FALSE(SocketAddressFromSockAddrStorage(addr_storage, &addr));
  EXPECT_EQ(from, addr);

  EXPECT_FALSE(SocketAddressFromSockAddrStorage(addr_storage, NULL));
}

TEST(SocketAddressTest, TestIPv4ToFromBuffer) {
  SocketAddress from("1.2.3.4", 5678), addr;
  char buf[20];
  EXPECT_TRUE(from.Write_(buf, sizeof(buf)));
  EXPECT_TRUE(addr.Read_(buf, sizeof(buf)));
  EXPECT_FALSE(addr.IsUnresolvedIP());
  EXPECT_EQ(AF_INET, addr.ipaddr().family());
  EXPECT_EQ(IPAddress(0x01020304U), addr.ipaddr());
  EXPECT_EQ(5678, addr.port());
  EXPECT_EQ("", addr.hostname());
  EXPECT_EQ("1.2.3.4:5678", addr.ToString());
}

TEST(SocketAddressTest, TestIPv6ToFromBuffer) {
  SocketAddress from6(kTestV6AddrString, 5678), addr;
  char buf[20];
  EXPECT_TRUE(from6.Write_(buf, sizeof(buf)));
  EXPECT_TRUE(addr.Read_(buf, sizeof(buf)));
  EXPECT_FALSE(addr.IsUnresolvedIP());
  EXPECT_EQ(AF_INET6, addr.ipaddr().family());
  EXPECT_EQ(IPAddress(kTestV6Addr), addr.ipaddr());
  EXPECT_EQ(5678, addr.port());
  EXPECT_EQ("", addr.hostname());
  EXPECT_EQ(kTestV6AddrFullString, addr.ToString());
}

TEST(SocketAddressTest, TestGoodResolve) {
  SocketAddress addr("localhost", 5678);
  int error;
  EXPECT_TRUE(addr.IsUnresolvedIP());
  EXPECT_TRUE(addr.ResolveIP(false, &error));
  EXPECT_EQ(0, error);
  EXPECT_FALSE(addr.IsUnresolvedIP());
  EXPECT_TRUE(addr.IsLoopbackIP());
  EXPECT_EQ(IPAddress(INADDR_LOOPBACK), addr.ipaddr());
  EXPECT_EQ(5678, addr.port());
  EXPECT_EQ("localhost", addr.hostname());
  EXPECT_EQ("localhost:5678", addr.ToString());
}

TEST(SocketAddressTest, TestBadResolve) {
  SocketAddress addr("address.bad", 5678);
  int error;
  EXPECT_TRUE(addr.IsUnresolvedIP());
  EXPECT_FALSE(addr.ResolveIP(false, &error));
  EXPECT_NE(0, error);
  EXPECT_TRUE(addr.IsUnresolvedIP());
}

bool AreEqual(const SocketAddress& addr1,
              const SocketAddress& addr2) {
  return addr1 == addr2 && addr2 == addr1 &&
      !(addr1 != addr2) && !(addr2 != addr1);
}

bool AreUnequal(const SocketAddress& addr1,
                const SocketAddress& addr2) {
  return !(addr1 == addr2) && !(addr2 == addr1) &&
      addr1 != addr2 && addr2 != addr1;
}

TEST(SocketAddressTest, TestEqualityOperators) {
  SocketAddress addr1("1.2.3.4", 5678);
  SocketAddress addr2("1.2.3.4", 5678);
  EXPECT_PRED2(AreEqual, addr1, addr2);

  addr2 = SocketAddress("0.0.0.1", 5678);
  EXPECT_PRED2(AreUnequal, addr1, addr2);

  addr2 = SocketAddress("1.2.3.4", 1234);
  EXPECT_PRED2(AreUnequal, addr1, addr2);

  addr2 = SocketAddress(kTestV6AddrString, 5678);
  EXPECT_PRED2(AreUnequal, addr1, addr2);

  addr1 = SocketAddress(kTestV6AddrString, 5678);
  EXPECT_PRED2(AreEqual, addr1, addr2);

  addr2 = SocketAddress(kTestV6AddrString, 1234);
  EXPECT_PRED2(AreUnequal, addr1, addr2);

  addr2 = SocketAddress("fe80::1", 5678);
  EXPECT_PRED2(AreUnequal, addr1, addr2);
}

bool IsLessThan(const SocketAddress& addr1,
                                      const SocketAddress& addr2) {
  return addr1 < addr2 &&
      !(addr2 < addr1) &&
      !(addr1 == addr2);
}

TEST(SocketAddressTest, TestComparisonOperator) {
  SocketAddress addr1("1.2.3.4", 5678);
  SocketAddress addr2("1.2.3.4", 5678);

  EXPECT_FALSE(addr1 < addr2);
  EXPECT_FALSE(addr2 < addr1);

  addr2 = SocketAddress("1.2.3.4", 5679);
  EXPECT_PRED2(IsLessThan, addr1, addr2);

  addr2 = SocketAddress("2.2.3.4", 49152);
  EXPECT_PRED2(IsLessThan, addr1, addr2);

  addr2 = SocketAddress(kTestV6AddrString, 5678);
  EXPECT_PRED2(IsLessThan, addr1, addr2);

  addr1 = SocketAddress("fe80::1", 5678);
  EXPECT_PRED2(IsLessThan, addr2, addr1);

  addr2 = SocketAddress("fe80::1", 5679);
  EXPECT_PRED2(IsLessThan, addr1, addr2);

  addr2 = SocketAddress("fe80::1", 5678);
  EXPECT_FALSE(addr1 < addr2);
  EXPECT_FALSE(addr2 < addr1);
}

}  // namespace talk_base
