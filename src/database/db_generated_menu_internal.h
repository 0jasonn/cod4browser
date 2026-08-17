#pragma once

struct statement_s;

// Canonical generated UI-expression closure shared by Menu and item loaders.
void DB_LoadGeneratedStatement(statement_s *statement, bool atStreamStart);
