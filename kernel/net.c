#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"
#include "net.h"

// xv6's ethernet and IP addresses
static uint8 local_mac[ETHADDR_LEN] = { 0x52, 0x54, 0x00, 0x12, 0x34, 0x56 };
static uint32 local_ip = MAKE_IP_ADDR(10, 0, 2, 15);

// qemu host's ethernet address.
static uint8 host_mac[ETHADDR_LEN] = { 0x52, 0x55, 0x0a, 0x00, 0x02, 0x02 };

static struct spinlock netlock;

// 存储接收到的UDP数据包信息
struct queued_packet {
  uint32 src_ip;
  uint16 src_port;
  char *payload; 
  int len; 
};

// UDP套接字结构
#define MAX_UDP_PACKETS 16
struct sock {
  int valid;                   
  int port;                     
  struct proc *proc;            
  struct spinlock lock;
  struct queued_packet *packets[MAX_UDP_PACKETS];
  int r_idx;                      
  int w_idx;                      
  int len;                        
};

// 全局套接字数组（每个进程可绑定一个端口）
static struct sock sockets[NPROC];

// 根据端口号查找套接字
static struct sock *
find_sock(int port)
{
  struct sock *s;
  for(s = sockets; s < &sockets[NPROC]; s++){
    if(s->valid == 1 && s->port == port) {
      return s;
    }
  }
  return 0;
}

void
netinit(void)
{
  initlock(&netlock, "netlock");
}

//
// bind(int port)
// prepare to receive UDP packets address to the port,
// i.e. allocate any queues &c needed.
//
uint64
sys_bind(void)
{
  struct proc *p = myproc();
  int port;
  struct sock *s = 0;
  argint(0, &port);
  acquire(&netlock);

  for(int i = 0; i < NPROC; i++){
    if(sockets[i].valid == 0){
      s = &sockets[i];
      break;
    }
  }

  if(s == 0){
    release(&netlock);
    return -1;
  }

  s->valid = 1;
  s->port = port;
  s->proc = p; 
  s->r_idx = 0;
  s->w_idx = 0;
  s->len = 0;
  initlock(&s->lock, "sock");
  release(&netlock);
  return 0;
}

//
// unbind(int port)
// release any resources previously created by bind(port);
// from now on UDP packets addressed to port should be dropped.
//
uint64
sys_unbind(void)
{
  //
  // Optional: Your code here.
  //
  return 0;
}

//
// recv(int dport, int *src, short *sport, char *buf, int maxlen)
// if there's a received UDP packet already queued that was
// addressed to dport, then return it.
// otherwise wait for such a packet.
//
// sets *src to the IP source address.
// sets *sport to the UDP source port.
// copies up to maxlen bytes of UDP payload to buf.
// returns the number of bytes copied,
// and -1 if there was an error.
//
// dport, *src, and *sport are host byte order.
// bind(dport) must previously have been called.
//
uint64
sys_recv(void)
{
  struct proc *p = myproc();
  int port;
  uint64 src_addr, sport_addr, buf_addr;
  int maxlen;
  struct sock *s = 0;
  struct queued_packet *qp;
  argint(0, &port);
  argaddr(1, &src_addr);
  argaddr(2, &sport_addr);
  argaddr(3, &buf_addr);
  argint(4, &maxlen);

  acquire(&netlock);

  s = find_sock(port);
  if(s == 0){
    release(&netlock);
    return -1;
  }

  acquire(&s->lock);
  release(&netlock);

  while(s->len == 0){
    if(p->killed) {
      release(&s->lock);
      return -1;
    }
    sleep(s, &s->lock);
  }

  qp = s->packets[s->r_idx];
  s->r_idx = (s->r_idx + 1) % MAX_UDP_PACKETS;
  s->len--;

  release(&s->lock);

  int copy_len = qp->len;
  if(copy_len > maxlen)
    copy_len = maxlen;

  if(copyout(p->pagetable, src_addr, (char*)&qp->src_ip, sizeof(qp->src_ip)) < 0 ||
     copyout(p->pagetable, sport_addr, (char*)&qp->src_port, sizeof(qp->src_port)) < 0 ||
     copyout(p->pagetable, buf_addr, qp->payload, copy_len) < 0) {
     if(qp->payload)
       kfree(qp->payload);
     kfree(qp);
     return -1;
  }

  if(qp->payload)
    kfree(qp->payload);
  kfree(qp);        
  return copy_len;
}

// This code is lifted from FreeBSD's ping.c, and is copyright by the Regents
// of the University of California.
static unsigned short
in_cksum(const unsigned char *addr, int len)
{
  int nleft = len;
  const unsigned short *w = (const unsigned short *)addr;
  unsigned int sum = 0;
  unsigned short answer = 0;

  /*
   * Our algorithm is simple, using a 32 bit accumulator (sum), we add
   * sequential 16 bit words to it, and at the end, fold back all the
   * carry bits from the top 16 bits into the lower 16 bits.
   */
  while (nleft > 1)  {
    sum += *w++;
    nleft -= 2;
  }

  /* mop up an odd byte, if necessary */
  if (nleft == 1) {
    *(unsigned char *)(&answer) = *(const unsigned char *)w;
    sum += answer;
  }

  /* add back carry outs from top 16 bits to low 16 bits */
  sum = (sum & 0xffff) + (sum >> 16);
  sum += (sum >> 16);
  /* guaranteed now that the lower 16 bits of sum are correct */

  answer = ~sum; /* truncate to 16 bits */
  return answer;
}

//
// send(int sport, int dst, int dport, char *buf, int len)
//
uint64
sys_send(void)
{
  struct proc *p = myproc();
  int sport;
  int dst;
  int dport;
  uint64 bufaddr;
  int len;

  argint(0, &sport);
  argint(1, &dst);
  argint(2, &dport);
  argaddr(3, &bufaddr);
  argint(4, &len);

  int total = len + sizeof(struct eth) + sizeof(struct ip) + sizeof(struct udp);
  if(total > PGSIZE)
    return -1;

  char *buf = kalloc();
  if(buf == 0){
    printf("sys_send: kalloc failed\n");
    return -1;
  }
  memset(buf, 0, PGSIZE);

  struct eth *eth = (struct eth *) buf;
  memmove(eth->dhost, host_mac, ETHADDR_LEN);
  memmove(eth->shost, local_mac, ETHADDR_LEN);
  eth->type = htons(ETHTYPE_IP);

  struct ip *ip = (struct ip *)(eth + 1);
  ip->ip_vhl = 0x45; // version 4, header length 4*5
  ip->ip_tos = 0;
  ip->ip_len = htons(sizeof(struct ip) + sizeof(struct udp) + len);
  ip->ip_id = 0;
  ip->ip_off = 0;
  ip->ip_ttl = 100;
  ip->ip_p = IPPROTO_UDP;
  ip->ip_src = htonl(local_ip);
  ip->ip_dst = htonl(dst);
  ip->ip_sum = in_cksum((unsigned char *)ip, sizeof(*ip));

  struct udp *udp = (struct udp *)(ip + 1);
  udp->sport = htons(sport);
  udp->dport = htons(dport);
  udp->ulen = htons(len + sizeof(struct udp));

  char *payload = (char *)(udp + 1);
  if(copyin(p->pagetable, payload, bufaddr, len) < 0){
    kfree(buf);
    printf("send: copyin failed\n");
    return -1;
  }

  e1000_transmit(buf, total);

  return 0;
}

void
ip_rx(char *buf, int len)
{
  // don't delete this printf; make grade depends on it.
  static int seen_ip = 0;
  if(seen_ip == 0)
    printf("ip_rx: received an IP packet\n");
  seen_ip = 1;

  struct eth *eth = (struct eth *) buf;
  struct ip *ip = (struct ip *) (eth + 1);
  struct udp *udp = (struct udp *) (ip + 1);

  int udp_len = ntohs(udp->ulen);
  if(udp_len < sizeof(struct udp) || len < sizeof(struct eth) + sizeof(struct ip) + udp_len)
    return;

  if(ip->ip_p != IPPROTO_UDP){
    kfree(buf);
    return;
  }

  int port = ntohs(udp->dport);

  acquire(&netlock);
  struct sock *s = find_sock(port);
  if(s == 0){
    kfree(buf);
    release(&netlock);
    return;
  }

  acquire(&s->lock);
  release(&netlock);

  if(s >= &sockets[NPROC] || s->len >= MAX_UDP_PACKETS){
    kfree(buf);
    release(&s->lock);
    return;
  }

  struct queued_packet *qp = kalloc();
  if(qp == 0){
    kfree(buf);
    release(&s->lock);
    return;
  }

  int payload_len = ntohs(udp->ulen) - sizeof(struct udp);
  char *payload_buf = kalloc();
  if(payload_buf == 0){
    kfree(qp); // must free the qp we just allocated
    kfree(buf);
    release(&s->lock);
    return;
  }

  memmove(payload_buf, (char*)(udp + 1), payload_len);
  memset(qp, 0, sizeof(*qp));

  qp->src_ip = ntohl(ip->ip_src);
  qp->src_port = ntohs(udp->sport);
  qp->payload = payload_buf;
  qp->len = payload_len;

  s->packets[s->w_idx] = qp;
  s->w_idx = (s->w_idx + 1) % MAX_UDP_PACKETS;
  s->len++;

  if(s->proc)
    wakeup(s);

  kfree(buf);

  release(&s->lock);
}

//
// send an ARP reply packet to tell qemu to map
// xv6's ip address to its ethernet address.
// this is the bare minimum needed to persuade
// qemu to send IP packets to xv6; the real ARP
// protocol is more complex.
//
void
arp_rx(char *inbuf)
{
  static int seen_arp = 0;

  if(seen_arp){
    kfree(inbuf);
    return;
  }
  printf("arp_rx: received an ARP packet\n");
  seen_arp = 1;

  struct eth *ineth = (struct eth *) inbuf;
  struct arp *inarp = (struct arp *) (ineth + 1);

  char *buf = kalloc();
  if(buf == 0)
    panic("send_arp_reply");
  
  struct eth *eth = (struct eth *) buf;
  memmove(eth->dhost, ineth->shost, ETHADDR_LEN); // ethernet destination = query source
  memmove(eth->shost, local_mac, ETHADDR_LEN); // ethernet source = xv6's ethernet address
  eth->type = htons(ETHTYPE_ARP);

  struct arp *arp = (struct arp *)(eth + 1);
  arp->hrd = htons(ARP_HRD_ETHER);
  arp->pro = htons(ETHTYPE_IP);
  arp->hln = ETHADDR_LEN;
  arp->pln = sizeof(uint32);
  arp->op = htons(ARP_OP_REPLY);

  memmove(arp->sha, local_mac, ETHADDR_LEN);
  arp->sip = htonl(local_ip);
  memmove(arp->tha, ineth->shost, ETHADDR_LEN);
  arp->tip = inarp->sip;

  e1000_transmit(buf, sizeof(*eth) + sizeof(*arp));

  kfree(inbuf);
}

void
net_rx(char *buf, int len)
{
  struct eth *eth = (struct eth *) buf;

  if(len >= sizeof(struct eth) + sizeof(struct arp) &&
     ntohs(eth->type) == ETHTYPE_ARP){
    arp_rx(buf);
  } else if(len >= sizeof(struct eth) + sizeof(struct ip) &&
     ntohs(eth->type) == ETHTYPE_IP){
    ip_rx(buf, len);
  } else {
    kfree(buf);
  }
}