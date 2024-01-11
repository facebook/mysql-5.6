#ifndef PROTOCOL_RPC_INCLUDED
#define PROTOCOL_RPC_INCLUDED

#include "sql/protocol.h"

class Protocol_RPC : public Protocol {
 public:
  enum enum_protocol_type type() const override { return PROTOCOL_RPC; }
  int read_packet() override { return 0; }
  int get_command(COM_DATA *, enum_server_command *) override { return 0; }
  enum enum_vio_type connection_type() const override { return NO_VIO_TYPE; }
  bool store_null() override { return false; }
  bool store_tiny(longlong, uint32) override { return false; }
  bool store_short(longlong, uint32) override { return false; }
  bool store_long(longlong, uint32) override { return false; }
  bool store_longlong(longlong, bool, uint32) override { return false; }
  bool store_decimal(const my_decimal *, uint, uint) override { return false; }
  bool store_float(float, uint32, uint32) override { return false; }
  bool store_double(double, uint32, uint32) override { return false; }
  bool store_datetime(const MYSQL_TIME &, uint) override { return false; }
  bool store_date(const MYSQL_TIME &) override { return false; }
  bool store_string(const char *, size_t, const CHARSET_INFO *) override {
    return false;
  }
  bool store_time(const MYSQL_TIME &, uint) override { return false; }
  bool store_field(const Field *) override { return false; }
  ulong get_client_capabilities() override { return m_client_capabilities; }
  bool has_client_capability(unsigned long client_capability) override {
    return (bool)(m_client_capabilities & client_capability);
  }
  bool connection_alive() const override { return true; }
  void start_row() override {
    // do nothing
  }
  bool end_row() override { return false; }
  void abort_row() override {
    // do nothing
  }
  void end_partial_result_set() override {
    // do nothing
  }

  int shutdown(bool) override { return 0; }
  uint get_rw_status() override { return 0; }
  bool get_compression() override { return false; }
  char *get_compression_algorithm() override { return nullptr; }
  uint get_compression_level() override { return 0; }
  bool send_ok(uint, uint, ulonglong, ulonglong, const char *,
               struct st_ok_metadata *) override {
    return false;
  }
  bool send_eof(uint, uint, struct st_ok_metadata *) override { return false; }
  bool send_error(uint, const char *, const char *) override { return false; }
  bool flush() override { return false; }
  bool store_ps_status(ulong, uint, uint, ulong) override { return false; }
  bool send_parameters(List<Item_param> *, bool) override { return false; }
  bool start_result_metadata(uint, uint, const CHARSET_INFO *) override {
    return false;
  }
  bool send_field_metadata(Send_field *, const CHARSET_INFO *) override {
    return false;
  }
  bool end_result_metadata() override { return false; }

 private:
  ulong m_client_capabilities;
};
#endif /* PROTOCOL_RPC_INCLUDED */
