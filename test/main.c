#include "mn_test.h"

void test_checksum(void);
void test_ether(void);
void test_link(void);
void test_arp(void);
void test_ipv4(void);
void test_icmp(void);
void test_net(void);
void test_udp(void);
void test_net_udp(void);
void test_dhcp(void);
void test_dns(void);
void test_ntp(void);
void test_tcp(void);

int main(void)
{
    printf("mega-net host tests\n");

    test_checksum();
    test_ether();
    test_link();
    test_arp();
    test_ipv4();
    test_icmp();
    test_net();
    test_udp();
    test_net_udp();
    test_dhcp();
    test_dns();
    test_ntp();
    test_tcp();

    printf("\n%d checks, %d failed\n", mn_tests_run, mn_tests_failed);

    return mn_tests_failed ? 1 : 0;
}
