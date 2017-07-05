#pragma once

#include "sql_class.h"

// Function called when parsing a COM_QUERY_ATTR.
// It checkes if it contains the RPC specific attributes and if it does it
// executes the query in a detached client session.
bool handle_com_rpc(THD *thd, char* packet, uint packet_length,
                    bool* is_rpc_query);

void srv_session_end_statement(Srv_session* session);
