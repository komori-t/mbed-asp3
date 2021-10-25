#include "mbed.h"

extern "C" {
    #include "kernel_rename.h"
    #include "t_stddef.h"
    #include "chip_serial.h"
    void target_fput_log(char c);
}

struct sio_port_control_block {
    intptr_t exinf;
};

static SIOPCB siopcb;
static UnbufferedSerial serial(USBTX, USBRX, 115200);

/*
 * SIOの割込みサービスルーチン
 */

void target_serial_tx_handler(void)
{
    sio_irdy_snd(siopcb.exinf);
}

void target_serial_rx_handler(void)
{
    sio_irdy_rcv(siopcb.exinf);
}

/*
 * SIOポートのオープン
 */
SIOPCB *sio_opn_por(ID siopid, intptr_t exinf)
{
    siopcb.exinf = exinf;
    return &siopcb;
}

/*
 * SIOポートのクローズ
 */
void sio_cls_por(SIOPCB *p_siopcb)
{
    serial.close();
}

/*
 * SIOポートへの文字送信
 */
bool_t sio_snd_chr(SIOPCB *p_siopcb, char c)
{
    if (serial.writable()) {
        serial.write(&c, 1);
        return true;
    }
    return false;
}

/*
 * SIOポートからの文字受信
 */
int_t sio_rcv_chr(SIOPCB *p_siopcb)
{
    if (serial.readable()) {
        char c;
        serial.read(&c, 1);
        return c;
    }
    return -1;
}

/*
 * SIOポートからのコールバックの許可
 */
void sio_ena_cbr(SIOPCB *p_siopcb, uint_t cbrtn)
{
    switch (cbrtn) {
        case SIO_RDY_SND:
            /* Enable TX FIFO interrupt */
            serial.attach(target_serial_tx_handler, SerialBase::TxIrq);
            break;
        case SIO_RDY_RCV:
            /* Enable RX FIFO interrupt */
            serial.attach(target_serial_rx_handler, SerialBase::RxIrq);
            break;
    }
}

/*
 * SIOポートからのコールバックの禁止
 */
void sio_dis_cbr(SIOPCB *p_siopcb, uint_t cbrtn)
{
    switch (cbrtn) {
        case SIO_RDY_SND:
            serial.attach(0, SerialBase::TxIrq);
            break;
        case SIO_RDY_RCV:
            serial.attach(0, SerialBase::RxIrq);
            break;
    }
}

/*
 * SIOポートへの文字出力
 */
void target_fput_log(char c)
{
    if (c == '\n') {
        while (! sio_snd_chr(&siopcb, '\r')) ;
    }
    while (! sio_snd_chr(&siopcb, c)) ;
}
