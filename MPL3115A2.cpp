/* Data sheet at https://www.nxp.com/docs/en/data-sheet/MPL3115A2.pdf */

#include <bit>
#include <ctime>

extern "C" {
#include <linux/i2c-dev.h>
#include <i2c/smbus.h>
}
#include <sys/ioctl.h>

#include <fcntl.h>
#include <unistd.h>

#include <cstring>
#include <stdexcept>

#include "MPL3115A2.hpp"


static const timespec fivemillisec = { 0, 5000000 };


MPL3115A2::MPL3115A2(int smbus, std::uint8_t address) : smbus(0), address(address)
{
	char name[20];
	snprintf(name, sizeof(name) - 1, "/dev/i2c-%d", smbus);
	this->smbus = open(name, O_RDWR);
	if (this->smbus < 0) {
		// Exceptions are usually avoided or forbidden in embedded code.
		// We like to avoid some ideoms from embedded programming
		// to realise the RAII principle.
		throw std::runtime_error("Device does not exist");
	}

	if (ioctl(this->smbus, I2C_SLAVE, address & 0x7f) < 0) {
		throw std::runtime_error("Address not found");
	}

	std::uint8_t whoami = i2c_smbus_read_byte_data(this->smbus, MPL3115A2::WHO_AM_I);
	if (whoami != 0xc4) {
		throw std::runtime_error("Not a MPL3115A2");
	}

	i2c_smbus_write_byte_data(this->smbus, MPL3115A2::CTRL_REG1, MPL3115A2::CTRL_REG1_RST);
	while (i2c_smbus_read_byte_data(this->smbus, MPL3115A2::CTRL_REG1) & MPL3115A2::CTRL_REG1_RST) {
		nanosleep(&fivemillisec, nullptr);
	}

	// set oversampling and altitude mode
	_ctrl_reg1.reg = MPL3115A2::CTRL_REG1_OS128 | MPL3115A2::CTRL_REG1_ALT;
	i2c_smbus_write_byte_data(this->smbus, MPL3115A2::CTRL_REG1, _ctrl_reg1.reg);

  	// enable data ready events for pressure/altitude and temperature
	i2c_smbus_write_byte_data(this->smbus, MPL3115A2::PT_DATA_CFG,
			          MPL3115A2::PT_DATA_CFG_TDEFE |
                                  MPL3115A2::PT_DATA_CFG_PDEFE |
                                  MPL3115A2::PT_DATA_CFG_DREM);
}

MPL3115A2::~MPL3115A2()
{
	close(this->smbus);
}



void MPL3115A2::_set_mode(std::uint8_t mode) noexcept
{
	this->_ctrl_reg1.reg = i2c_smbus_read_byte_data(this->smbus, MPL3115A2::CTRL_REG1);
	this->_ctrl_reg1.bit.ALT = mode;
	i2c_smbus_write_byte_data(this->smbus, MPL3115A2::CTRL_REG1, this->_ctrl_reg1.reg);
}

void MPL3115A2::_one_shot(void) noexcept
{
	this->_ctrl_reg1.reg = i2c_smbus_read_byte_data(this->smbus, MPL3115A2::CTRL_REG1);
	while (this->_ctrl_reg1.bit.OST) {
		nanosleep(&fivemillisec, nullptr);
		this->_ctrl_reg1.reg = i2c_smbus_read_byte_data(this->smbus, MPL3115A2::CTRL_REG1);
	}
	this->_ctrl_reg1.bit.OST = 1;
	i2c_smbus_write_byte_data(this->smbus, MPL3115A2::CTRL_REG1, this->_ctrl_reg1.reg);
}

void MPL3115A2::_await_completion(std::uint8_t status) noexcept
{
	while (0 == (i2c_smbus_read_byte_data(this->smbus, MPL3115A2::STATUS) & status)) {
		nanosleep(&fivemillisec, nullptr);
	}
}
			


float MPL3115A2::pressure(void) noexcept
{
	this->_set_mode(0);
	this->_one_shot();
	this->_await_completion();
	i2c_smbus_read_i2c_block_data(this->smbus, MPL3115A2::OUT_P_MSB, 5, this->buffer);
	std::uint32_t p;
	p = std::uint32_t(this->buffer[0]) * 65536 + std::uint32_t(this->buffer[1]) * 256 + std::uint32_t(this->buffer[2]);
	return float(p) / 6400.0;
}


float MPL3115A2::altitude(void) noexcept
{
	this->_set_mode(1);
	this->_one_shot();
	this->_await_completion();
	i2c_smbus_read_i2c_block_data(this->smbus, MPL3115A2::OUT_P_MSB, 5, this->buffer);
	std::uint32_t a;
	a = std::uint32_t(this->buffer[0]) * 16777216 + std::uint32_t(this->buffer[1]) * 65536 + std::uint32_t(this->buffer[2]) * 256;
	return float(a) / 65536.0;
}


float MPL3115A2::temperature(void) noexcept
{
	this->_one_shot();
	this->_await_completion();
	i2c_smbus_read_i2c_block_data(this->smbus, MPL3115A2::OUT_P_MSB, 5, this->buffer);
	std::uint32_t t;
	t = std::uint32_t(this->buffer[3])* 256 + std::uint32_t(this->buffer[4]);
	return float(t) / 256.0;
}
