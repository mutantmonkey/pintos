
#include <stdio.h>
#include "devices/pci.h"
#include "devices/e1000.h"
#include "lib/string.h"
#include "threads/vaddr.h"

struct tx_desc tx_desc_array[E1000_TXDESC] __attribute__ ((aligned (16)));
struct tx_pkt tx_pkt_bufs[E1000_TXDESC];

struct rcv_desc rcv_desc_array[E1000_RCVDESC] __attribute__ ((aligned (16)));
struct rcv_pkt rcv_pkt_bufs[E1000_RCVDESC];


void
e1000_init(void)
{
  uint32_t i = 0;
  printf("E1000 start\n");
  struct pci_dev *pci = pci_get_device(E1000_VENDOR, E1000_DEVICE, 0, 0);
  pci_enable(pci);
  
  struct pci_io *io = pci_io_enum(pci, NULL);
  
  printf("%08x \n", pci_reg_read32(io, E1000_STATUS)); 
  
  ASSERT(pci_reg_read32(io, E1000_STATUS) == 0x80080783);
  memset(tx_desc_array, 0x0, sizeof(struct tx_desc) * E1000_TXDESC);
  memset(tx_pkt_bufs, 0x0, sizeof(struct tx_pkt) * E1000_TXDESC);
  for (i = 0; i < E1000_TXDESC; i++) 
  {
    tx_desc_array[i].addr = vtop(&tx_pkt_bufs[i].buf);
    tx_desc_array[i].status |= E1000_TXD_STAT_DD;
  }

  memset(rcv_desc_array, 0x0, sizeof(struct rcv_desc) * E1000_RCVDESC);
  memset(rcv_pkt_bufs, 0x0, sizeof(struct rcv_pkt) * E1000_RCVDESC);
  for (i = 0; i < E1000_RCVDESC; i++) 
  {
    rcv_desc_array[i].addr = vtop(&rcv_pkt_bufs[i].buf);
  }

  pci_reg_write32(io, E1000_TDBAL, vtop(tx_desc_array)); 
  pci_reg_write32(io, E1000_TDBAH, 0x0);
  

  pci_reg_write32(io, E1000_TDLEN, sizeof(struct tx_desc) * E1000_TXDESC);

  pci_reg_write32(io, E1000_TDH, 0x0);
  pci_reg_write32(io, E1000_TDT, 0x0);

  uint32_t tmp = pci_reg_read32(io, E1000_TCTL);
  tmp |= E1000_TCTL_EN;
  tmp |= E1000_TCTL_PSP;
  tmp &= ~E1000_TCTL_CT;
  tmp |= (0x10) << 4;
  tmp &= ~E1000_TCTL_COLD;
  tmp |= (0x40) << 12;
  pci_reg_write32(io, E1000_TCTL, tmp);


  tmp = pci_reg_read32(io, E1000_TIPG);
  tmp = 0x0;
  tmp |= (0x6) << 20; // IPGR2 
  tmp |= (0x4) << 10; // IPGR1
  tmp |= 0xA; // IPGR
  pci_reg_write32(io, E1000_TIPG, tmp);


   
  pci_reg_write32(io, E1000_EERD, 0x0);
  pci_reg_write32(io, E1000_EERD, pci_reg_read32(io, E1000_EERD) | E1000_EERD_START);
  
  while( ! (pci_reg_read32(io, E1000_EERD) & E1000_EERD_DONE));
      pci_reg_write32(io, E1000_RAL, pci_reg_read32(io, E1000_EERD) >> 16);
 
  
  pci_reg_write32(io, E1000_EERD, 0x1 << 8);
  pci_reg_write32(io, E1000_EERD, pci_reg_read32(io, E1000_EERD) | E1000_EERD_START);	
  
  while( ! (pci_reg_read32(io, E1000_EERD) & E1000_EERD_DONE));
  pci_reg_write32(io, E1000_RAL, pci_reg_read32(io, E1000_RAL) | (pci_reg_read32(io, E1000_EERD) & 0xffff0000));

  pci_reg_write32(io, E1000_EERD, 0x2 << 8);
  pci_reg_write32(io, E1000_EERD, pci_reg_read32(io, E1000_EERD) | E1000_EERD_START);	

  while( ! (pci_reg_read32(io, E1000_EERD) & E1000_EERD_DONE));
  pci_reg_write32(io, E1000_RAH, pci_reg_read32(io, E1000_EERD) >> 16);
 
  pci_reg_write32(io, E1000_RAH, 0x1 << 31);

  pci_reg_write32(io, E1000_RDBAL, vtop(rcv_desc_array));
  pci_reg_write32(io, E1000_RDBAH, 0x0);
        
  pci_reg_write32(io, E1000_RDLEN, sizeof(struct rcv_desc) * E1000_RCVDESC);

  pci_reg_write32(io, E1000_RDH, 0x0);
  pci_reg_write32(io, E1000_RDT, 0x0);

  tmp = pci_reg_read32(io, E1000_RCTL);
  tmp |= E1000_RCTL_EN;
  tmp &= ~E1000_RCTL_LPE;
  tmp &= ~E1000_RCTL_LBM;
  tmp &= ~E1000_RCTL_RDMTS;
  tmp &= ~E1000_RCTL_MO;
  tmp |= E1000_RCTL_BAM;
  tmp &= ~E1000_RCTL_SZ; // 2048 byte size
  tmp |= E1000_RCTL_SECRC;
  pci_reg_write32(io, E1000_RCTL, tmp);
  
  //return 0;
}


int
e1000_transmit(char *data, int len)
{
  if (len > TX_PKT_SIZE) 
  {
    return -E_PKT_TOO_LONG;
  }

  uint32_t tdt = e1000[E1000_TDT];
  if (tx_desc_array[tdt].status & E1000_TXD_STAT_DD) 
  {
    memmove(tx_pkt_bufs[tdt].buf, data, len);
    tx_desc_array[tdt].length = len;
    //HEXDUMP("tx dump:", data, len);

    tx_desc_array[tdt].status &= ~E1000_TXD_STAT_DD;
    tx_desc_array[tdt].cmd |= E1000_TXD_CMD_RS;
    tx_desc_array[tdt].cmd |= E1000_TXD_CMD_EOP;

    e1000[E1000_TDT] = (tdt + 1) % E1000_TXDESC;
  }
  else 
  { // tx queue is full
    return -E_TX_FULL;
  }

  return 0;
}


int
e1000_receive(char *data)
{
  uint32_t rdt, len;
  rdt = e1000[E1000_RDT];

  if (rcv_desc_array[rdt].status & E1000_RXD_STAT_DD) 
  {
    if (!(rcv_desc_array[rdt].status & E1000_RXD_STAT_EOP)) 
    {
      PANIC("Don't allow jumbo frames!\n");
    }
    len = rcv_desc_array[rdt].length;

    memmove(data, rcv_pkt_bufs[rdt].buf, len);
    //HEXDUMP("rx dump:", data, len);
    rcv_desc_array[rdt].status &= ~E1000_RXD_STAT_DD;
    rcv_desc_array[rdt].status &= ~E1000_RXD_STAT_EOP;
    e1000[E1000_RDT] = (rdt + 1) % E1000_RCVDESC;

    return len;
  }
  return -E_RCV_EMPTY;
}
