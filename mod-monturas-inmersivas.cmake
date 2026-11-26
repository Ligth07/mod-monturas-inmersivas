# MySQLPreparedStatement.h uses the client headers from AzerothCore's mysql target.
GetProjectNameOfModuleName("${SOURCE_MODULE}" MONTURAS_TARGET)
if(TARGET "${MONTURAS_TARGET}")
  target_link_libraries("${MONTURAS_TARGET}" PRIVATE mysql)
elseif(MODULE_MOD-MONTURAS-INMERSIVAS STREQUAL "static")
  target_link_libraries(modules PRIVATE mysql)
endif()
