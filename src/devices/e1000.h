
#define E1000_VENDOR		0x8086
#define E1000_DEVICE		0x100e


//volatile uint32_t *e1000; // MMIO address to access E1000 BAR
#define E1000_MMIOADDR KSTACKTOP /* MMIO Address for E1000 BAR 0 */
#define E1000_TXDESC 64
#define E1000_RCVDESC 64
#define TX_PKT_SIZE 1518
#define RCV_PKT_SIZE 2048

// Register Set
#define E1000_CTRL     0x00000      /* Device Control */

#define E1000_STATUS   0x00008///4  /* Device Status - RO */
#define E1000_EERD     0x00014///4  /* EEPROM Read - RW */

#define E1000_EERD_START 0x01
#define E1000_EERD_DONE  0x10

#define E1000_FCAL     0x00028      /* Flow Control Address Low */
#define E1000_FCAH     0x0002c      /* Flow Control Address High */
#define E1000_FCT      0x00030      /* Flow Control Type */
#define E1000_FCTTV    0x00170      /* Flow Control Transmit Timer Value */

#define E1000_TDBAL    0x03800///4  /* TX Descriptor Base Address Low - RW */
#define E1000_TDBAH    0x03804///4  /* TX Descriptor Base Address High - RW */
#define E1000_TDLEN    0x03808///4  /* TX Descriptor Length - RW */
#define E1000_TDH      0x03810///4  /* TX Descriptor Head - RW */
#define E1000_TDT      0x03818///4  /* TX Descripotr Tail - RW */

#define E1000_RDBAL    0x02800///4  /* RX Descriptor Base Address Low - RW */
#define E1000_RDBAH    0x02804///4  /* RX Descriptor Base Address High - RW */
#define E1000_RDLEN    0x02808///4  /* RX Descriptor Length - RW */
#define E1000_RDH      0x02810///4  /* RX Descriptor Head - RW */
#define E1000_RDT      0x02818///4  /* RX Descriptor Tail - RW */
#define E1000_RAL      0x05400///4  /* Receive Address Low - RW */
#define E1000_RAH      0x05404///4  /* Receive Address High - RW */

#define E1000_TCTL     0x00400///4  /* TX Control - RW */
#define E1000_TCTL_EN     0x00000002    /* enable tx */
#define E1000_TCTL_PSP    0x00000008    /* pad short packets */
#define E1000_TCTL_CT     0x00000ff0    /* collision threshold */
#define E1000_TCTL_COLD   0x003ff000    /* collision distance */

#define E1000_RCTL     0x00100///4  /* RX Control - RW */
#define E1000_RCTL_EN             0x00000002    /* enable */
#define E1000_RCTL_UPE            0x00000004    /* Unicast Promiscuous Enable */
#define E1000_RCTL_MPE            0x00000008    /* Multicast Promiscuous Enable */
#define E1000_RCTL_LPE            0x00000020    /* long packet enable */
#define E1000_RCTL_LBM            0x000000C0    /* loopback mode */
#define E1000_RCTL_RDMTS          0x00000300    /* rx min threshold size */
#define E1000_RCTL_MO             0x00003000    /* multicast offset shift */
#define E1000_RCTL_BAM            0x00008000    /* broadcast enable */
#define E1000_RCTL_SZ             0x00030000    /* rx buffer size */
#define E1000_RCTL_SECRC          0x04000000    /* Strip Ethernet CRC */

#define E1000_TIPG     0x00410///4  /* TX Inter-packet gap -RW */

/* Control Register bit defintions */
#define E1000_CTRL_ASDE      (1 << 5)   /* Auto-Speed Detection Enable */
#define E1000_CTRL_LRST      (1 << 3)   /* Link Reset */
#define E1000_CTRL_SLU       (1 << 6)   /* Set Link Up */
#define E1000_CTRL_ILOS      (1 << 7)   /* Invert Loss-of-Signal */
#define E1000_CTRL_RST       (1 << 26)  /* Device Reset */
#define E1000_CTRL_VME       (1 << 30)  /* VLAN Mode Enable */
#define E1000_CTRL_PHY_RST   (1 << 31)  /* PHY Reset */

/* Transmit Descriptor bit definitions */
#define E1000_TXD_CMD_RS     0x00000008 /* Report Status */
#define E1000_TXD_CMD_EOP    0x00000001 /* End of Packet */
#define E1000_TXD_STAT_DD    0x00000001 /* Descriptor Done */

/* Receive Descriptor bit definitions */
#define E1000_RXD_STAT_DD       0x01    /* Descriptor Done */
#define E1000_RXD_STAT_EOP      0x02    /* End of Packet */

/* Receive Address High bit defintions */
#define E1000_RAH_AV            (1 << 31)   /* Receive Address Valid */

enum
{
  //E1000 error codes
  E_TX_FULL       = 16,   // Transfer queue is full
  E_RCV_EMPTY     = 17,   // Receive queue is empty
  E_PKT_TOO_LONG  = 18   // Packet size is too big
};

/*struct tx_desc
{
	uint64_t addr;
	uint16_t length;
	uint8_t cso;
	uint8_t cmd;
	uint8_t status;
	uint8_t css;
	uint16_t special;
} __attribute__((packed));

struct rx_desc
{
	uint64_t addr;
	uint16_t length;
	uint16_t chksum;
	uint8_t status;
	uint8_t errors;
	uint16_t special;
} __attribute__((packed));*/

struct rx_desc
{
    uint64_t buffer;
    uint16_t length;
    uint16_t checksum;
    uint8_t  status;
    uint8_t  errors;
    uint16_t special;
};

struct tx_desc
{
    uint64_t buffer;
    uint16_t length;
    uint8_t  checksum_off;
    uint8_t  command;
    uint8_t  status;
    uint8_t  checksum_st;
    uint16_t special;
};

struct tx_pkt
{
	uint8_t buf[TX_PKT_SIZE];
} __attribute__((packed));

struct rx_pkt
{
	uint8_t buf[RCV_PKT_SIZE];
} __attribute__((packed));



void e1000_init(void);
int e1000_transmit(char *data, size_t len);
int e1000_receive(char *data, size_t len);


