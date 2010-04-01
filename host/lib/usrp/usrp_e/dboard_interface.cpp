//
// Copyright 2010 Ettus Research LLC
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//

#include "usrp_e_impl.hpp"
#include "usrp_e_regs.hpp"
#include <uhd/types/dict.hpp>
#include <uhd/utils/assert.hpp>
#include <boost/assign/list_of.hpp>
#include <algorithm> //std::copy
#include <linux/usrp_e.h>

using namespace uhd::usrp;

class usrp_e_dboard_interface : public dboard_interface{
public:
    usrp_e_dboard_interface(usrp_e_impl *impl);
    ~usrp_e_dboard_interface(void);

    void write_aux_dac(unit_type_t, int, int);
    int read_aux_adc(unit_type_t, int);

    void set_atr_reg(gpio_bank_t, atr_reg_t, boost::uint16_t);
    void set_gpio_ddr(gpio_bank_t, boost::uint16_t);
    boost::uint16_t read_gpio(gpio_bank_t);

    void write_i2c(int, const byte_vector_t &);
    byte_vector_t read_i2c(int, size_t);

    double get_rx_clock_rate(void);
    double get_tx_clock_rate(void);

private:
    byte_vector_t transact_spi(
        spi_dev_t dev,
        spi_edge_t edge,
        const byte_vector_t &buf,
        bool readback
    );

    usrp_e_impl *_impl;
};

/***********************************************************************
 * Make Function
 **********************************************************************/
dboard_interface::sptr make_usrp_e_dboard_interface(usrp_e_impl *impl){
    return dboard_interface::sptr(new usrp_e_dboard_interface(impl));
}

/***********************************************************************
 * Structors
 **********************************************************************/
usrp_e_dboard_interface::usrp_e_dboard_interface(usrp_e_impl *impl){
    _impl = impl;
}

usrp_e_dboard_interface::~usrp_e_dboard_interface(void){
    /* NOP */
}

/***********************************************************************
 * Clock Rates
 **********************************************************************/
double usrp_e_dboard_interface::get_rx_clock_rate(void){
    throw std::runtime_error("not implemented");
}

double usrp_e_dboard_interface::get_tx_clock_rate(void){
    throw std::runtime_error("not implemented");
}

/***********************************************************************
 * GPIO
 **********************************************************************/
void usrp_e_dboard_interface::set_gpio_ddr(gpio_bank_t bank, boost::uint16_t value){
    //define mapping of gpio bank to register address
    static const uhd::dict<gpio_bank_t, boost::uint32_t> bank_to_addr = boost::assign::map_list_of
        (GPIO_BANK_RX, UE_REG_GPIO_RX_DDR)
        (GPIO_BANK_TX, UE_REG_GPIO_TX_DDR)
    ;
    _impl->poke16(bank_to_addr[bank], value);
}

boost::uint16_t usrp_e_dboard_interface::read_gpio(gpio_bank_t bank){
    //define mapping of gpio bank to register address
    static const uhd::dict<gpio_bank_t, boost::uint32_t> bank_to_addr = boost::assign::map_list_of
        (GPIO_BANK_RX, UE_REG_GPIO_RX_IO)
        (GPIO_BANK_TX, UE_REG_GPIO_TX_IO)
    ;
    return _impl->peek16(bank_to_addr[bank]);
}

void usrp_e_dboard_interface::set_atr_reg(gpio_bank_t bank, atr_reg_t atr, boost::uint16_t value){
    //define mapping of bank to atr regs to register address
    static const uhd::dict<
        gpio_bank_t, uhd::dict<atr_reg_t, boost::uint32_t>
    > bank_to_atr_to_addr = boost::assign::map_list_of
        (GPIO_BANK_RX, boost::assign::map_list_of
            (ATR_REG_IDLE,        UE_REG_ATR_IDLE_RXSIDE)
            (ATR_REG_TX_ONLY,     UE_REG_ATR_INTX_RXSIDE)
            (ATR_REG_RX_ONLY,     UE_REG_ATR_INRX_RXSIDE)
            (ATR_REG_FULL_DUPLEX, UE_REG_ATR_FULL_RXSIDE)
        )
        (GPIO_BANK_TX, boost::assign::map_list_of
            (ATR_REG_IDLE,        UE_REG_ATR_IDLE_TXSIDE)
            (ATR_REG_TX_ONLY,     UE_REG_ATR_INTX_TXSIDE)
            (ATR_REG_RX_ONLY,     UE_REG_ATR_INRX_TXSIDE)
            (ATR_REG_FULL_DUPLEX, UE_REG_ATR_FULL_TXSIDE)
        )
    ;
    _impl->poke16(bank_to_atr_to_addr[bank][atr], value);
}

/***********************************************************************
 * SPI
 **********************************************************************/
dboard_interface::byte_vector_t usrp_e_dboard_interface::transact_spi(
    spi_dev_t dev,
    spi_edge_t edge,
    const byte_vector_t &buf,
    bool readback
){
    //load data struct
    usrp_e_spi data;
    data.readback = (readback)? UE_SPI_TXRX : UE_SPI_TXONLY;
    data.slave = (dev == SPI_DEV_RX)? UE_SPI_CTRL_RXNEG : UE_SPI_CTRL_TXNEG;
    data.length = buf.size() * 8; //bytes to bits
    boost::uint8_t *data_bytes = reinterpret_cast<boost::uint8_t*>(&data.data);

    //load the data
    ASSERT_THROW(buf.size() <= sizeof(data.data));
    std::copy(buf.begin(), buf.end(), data_bytes);

    //load the flags
    data.flags = 0;
    data.flags |= (edge == SPI_EDGE_RISE)? UE_SPI_LATCH_RISE : UE_SPI_LATCH_FALL;
    data.flags |= (edge == SPI_EDGE_RISE)? UE_SPI_PUSH_RISE  : UE_SPI_PUSH_FALL;

    //call the spi ioctl
    _impl->ioctl(USRP_E_SPI, &data);

    //unload the data
    byte_vector_t ret(data.length/8); //bits to bytes
    ASSERT_THROW(ret.size() <= sizeof(data.data));
    std::copy(data_bytes, data_bytes+ret.size(), ret.begin());
    return ret;
}

/***********************************************************************
 * I2C
 **********************************************************************/
static const size_t max_i2c_data_bytes = 10;

void usrp_e_dboard_interface::write_i2c(int i2c_addr, const byte_vector_t &buf){
    //allocate some memory for this transaction
    ASSERT_THROW(buf.size() <= max_i2c_data_bytes);
    boost::uint8_t mem[sizeof(usrp_e_i2c) + max_i2c_data_bytes];

    //load the data struct
    usrp_e_i2c &data = reinterpret_cast<usrp_e_i2c&>(mem);
    data.addr = i2c_addr;
    data.len = buf.size();
    std::copy(buf.begin(), buf.end(), data.data);

    //call the spi ioctl
    _impl->ioctl(USRP_E_I2C_WRITE, &data);
}

dboard_interface::byte_vector_t usrp_e_dboard_interface::read_i2c(int i2c_addr, size_t num_bytes){
    //allocate some memory for this transaction
    ASSERT_THROW(num_bytes <= max_i2c_data_bytes);
    boost::uint8_t mem[sizeof(usrp_e_i2c) + max_i2c_data_bytes];

    //load the data struct
    usrp_e_i2c &data = reinterpret_cast<usrp_e_i2c&>(mem);
    data.addr = i2c_addr;
    data.len = num_bytes;

    //call the spi ioctl
    _impl->ioctl(USRP_E_I2C_READ, &data);

    //unload the data
    byte_vector_t ret(data.len);
    ASSERT_THROW(ret.size() == num_bytes);
    std::copy(data.data, data.data+ret.size(), ret.begin());
    return ret;
}

/***********************************************************************
 * Aux DAX/ADC
 **********************************************************************/
void usrp_e_dboard_interface::write_aux_dac(dboard_interface::unit_type_t unit, int which, int value){
    throw std::runtime_error("not implemented");
}

int usrp_e_dboard_interface::read_aux_adc(dboard_interface::unit_type_t unit, int which){
    throw std::runtime_error("not implemented");
}
