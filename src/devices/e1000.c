
#include <stdio.h>
#include "devices/pci.h"
#include "devices/e1000.h"
#include "lib/string.h"
#include "threads/vaddr.h"

struct tx_desc tx_desc_array[E1000_TXDESC] __attribute__ ((aligned (16)));
struct tx_pkt tx_pkt_bufs[E1000_TXDESC];

struct rx_desc rx_desc_array[E1000_RCVDESC] __attribute__ ((aligned (16)));
struct rx_pkt rx_pkt_bufs[E1000_RCVDESC];

struct pci_io *io;

static void e1000_init_buffers(struct pci_io *io);
static void e1000_reg_set(struct pci_io *io, uint32_t, uint32_t);
static void e1000_reg_unset(struct pci_io *io, uint32_t, uint32_t);

void
e1000_init(void)
{
  printf("E1000 start\n");

  // enable PCI device
  struct pci_dev *pci = pci_get_device(E1000_VENDOR, E1000_DEVICE, 0, 0);
  pci_enable(pci);
  
  //struct pci_io *io = pci_io_enum(pci, NULL);
  io = pci_io_enum(pci, NULL);
  printf("%08x \n", pci_reg_read32(io, E1000_STATUS)); 

  ASSERT(pci_reg_read32(io, E1000_STATUS) == 0x80080783);

  // initialize as described in section 14.3 of the Intel Gigabit Ethernet
  // Controllers Software Developer's Manual
  e1000_reg_set   (io, E1000_CTRL, E1000_CTRL_ASDE | E1000_CTRL_SLU);
  e1000_reg_unset (io, E1000_CTRL, E1000_CTRL_LRST);
  e1000_reg_unset (io, E1000_CTRL, E1000_CTRL_PHY_RST);
  e1000_reg_unset (io, E1000_CTRL, E1000_CTRL_ILOS);
  pci_reg_write32 (io, E1000_FCAL, 0x0);
  pci_reg_write32 (io, E1000_FCAH, 0x0);
  pci_reg_write32 (io, E1000_FCT, 0x0);
  pci_reg_write32 (io, E1000_FCTTV, 0x0);
  e1000_reg_unset (io, E1000_CTRL, E1000_CTRL_VME);

  // set rx address registers
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
 
  // set address valid bit
  e1000_reg_set   (io, E1000_RAH, E1000_RAH_AV);

  // enable multicast promiscuous mode
  e1000_reg_set   (io, E1000_RCTL, E1000_RCTL_MPE);

  e1000_init_buffers (io);

  // set tx IPG register
  pci_reg_write32 (io, E1000_TIPG, 0x0);
  e1000_reg_set   (io, E1000_TIPG, (0x6) << 20); // IPGR2
  e1000_reg_set   (io, E1000_TIPG, (0x4) << 10); // IPGR1
  e1000_reg_set   (io, E1000_TIPG, 0xA);         // IPGR
}

static void
e1000_init_buffers(struct pci_io *io)
{
  uint32_t i = 0;

  // initialize rx buffer array
  memset(rx_desc_array, 0x0, sizeof(struct rx_desc) * E1000_RCVDESC);
  memset(rx_pkt_bufs, 0x0, sizeof(struct rx_pkt) * E1000_RCVDESC);
  for (i = 0; i < E1000_RCVDESC; i++) 
  {
    rx_desc_array[i].buffer = vtop(&rx_pkt_bufs[i].buf);
    rx_desc_array[i].status = 0;
  }

  // initialize tx buffer array
  memset(tx_desc_array, 0x0, sizeof(struct tx_desc) * E1000_TXDESC);
  memset(tx_pkt_bufs, 0x0, sizeof(struct tx_pkt) * E1000_TXDESC);
  for (i = 0; i < E1000_TXDESC; i++) 
  {
    tx_desc_array[i].buffer = vtop(&tx_pkt_bufs[i].buf);
    tx_desc_array[i].status = E1000_TXD_STAT_DD;
  }

  // setup rx ring registers
  pci_reg_write32 (io, E1000_RDBAL, vtop (rx_desc_array));
  pci_reg_write32 (io, E1000_RDBAH, 0x0);
  pci_reg_write32 (io, E1000_RDLEN, sizeof(struct rx_desc) * E1000_RCVDESC);
  pci_reg_write32 (io, E1000_RDH, 0x0);
  pci_reg_write32 (io, E1000_RDT, 0x0);
  e1000_reg_unset (io, E1000_RCTL, E1000_RCTL_SZ);
  e1000_reg_set   (io, E1000_RCTL, E1000_RCTL_EN);

  // set RCTL bits (same as used in JOS)
  e1000_reg_unset (io, E1000_RCTL, E1000_RCTL_LPE);
  e1000_reg_unset (io, E1000_RCTL, E1000_RCTL_LBM);
  e1000_reg_unset (io, E1000_RCTL, E1000_RCTL_RDMTS);
  e1000_reg_unset (io, E1000_RCTL, E1000_RCTL_MO);
  e1000_reg_set   (io, E1000_RCTL, E1000_RCTL_BAM);
  e1000_reg_set   (io, E1000_RCTL, E1000_RCTL_SECRC);

  // setup tx ring registers
  pci_reg_write32 (io, E1000_TDBAL, vtop (tx_desc_array));
  pci_reg_write32 (io, E1000_TDBAH, 0x0);
  pci_reg_write32 (io, E1000_TDLEN, sizeof(struct tx_desc) * E1000_TXDESC);
  pci_reg_write32 (io, E1000_TDH, 0x0);
  pci_reg_write32 (io, E1000_TDT, 0x0);
  e1000_reg_set   (io, E1000_TCTL, E1000_TCTL_EN | E1000_TCTL_PSP);

  // set TCTL bits (same as used by JOS)
  e1000_reg_unset (io, E1000_TCTL, E1000_TCTL_CT);
  e1000_reg_set   (io, E1000_TCTL, (0x10) << 4);
  e1000_reg_unset (io, E1000_TCTL, E1000_TCTL_COLD);
  e1000_reg_set   (io, E1000_TCTL, (0x40) << 12);
}

/* Set the value of an e1000 register.
   Inspired by the helper function in Minix. */
static void
e1000_reg_set(struct pci_io *io, uint32_t reg, uint32_t value)
{
  uint32_t data;

  data = pci_reg_read32 (io, reg);
  pci_reg_write32 (io, reg, data | value);
}

/* Unset the value of an e1000 register.
   Inspired by the helper function in Minix. */
static void
e1000_reg_unset(struct pci_io *io, uint32_t reg, uint32_t value)
{
  uint32_t data;

  data = pci_reg_read32 (io, reg);
  pci_reg_write32 (io, reg, data & ~value);
}

int
e1000_transmit(char *data, size_t len)
{
  if (len > TX_PKT_SIZE) 
  {
    return -E_PKT_TOO_LONG;
  }

  //uint32_t tdt = e1000[E1000_TDT];
  uint32_t tdt = pci_reg_read32(io, E1000_TDT);
  if (tx_desc_array[tdt].status & E1000_TXD_STAT_DD) 
  {
    memmove(tx_pkt_bufs[tdt].buf, data, len);
    tx_desc_array[tdt].length = len;
    //HEXDUMP("tx dump:", data, len);

    tx_desc_array[tdt].status &= ~E1000_TXD_STAT_DD;
    tx_desc_array[tdt].command |= E1000_TXD_CMD_RS;
    tx_desc_array[tdt].command |= E1000_TXD_CMD_EOP;

    //e1000[E1000_TDT] = (tdt + 1) % E1000_TXDESC;
    pci_reg_write32(io, E1000_TDT, (tdt + 1) % E1000_TXDESC);
  }
  else 
  { // tx queue is full
    return -E_TX_FULL;
  }

  return 0;
}


int
e1000_receive(char *data, size_t len)
{
  uint32_t rdt;
  rdt = pci_reg_read32 (io, E1000_RDT);

  if (rx_desc_array[rdt].status & E1000_RXD_STAT_DD) 
  {
    /*if (!(rx_desc_array[rdt].status & E1000_RXD_STAT_EOP)) 
    {
      PANIC("Don't allow jumbo frames!\n");
    }*/

    if (rx_desc_array[rdt].length < len)
        len = rx_desc_array[rdt].length;

    memmove(data, rx_pkt_bufs[rdt].buf, len);
    hex_dump(0, data, len, true);

    rx_desc_array[rdt].status &= ~E1000_RXD_STAT_DD;
    //rx_desc_array[rdt].status &= ~E1000_RXD_STAT_EOP;

    pci_reg_write32(io, E1000_RDT, (rdt + 1) % E1000_RCVDESC);

    return len;
  }

  return -E_RCV_EMPTY;
}
