#pragma once

#include "sql_class.h"

// Function called when parsing a COM_QUERY_ATTR.
// It checkes if it contains the RPC specific attributes and if it does it
// executes the query in a detached client session.
std::pair<bool, std::shared_ptr<Srv_session>> handle_com_rpc(THD *thd);
void cleanup_com_rpc(
    THD *thd,
    std::shared_ptr<Srv_session> srv_session,
    bool state_changed);
void srv_session_end_statement(Srv_session& session);

