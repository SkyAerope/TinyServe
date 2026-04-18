#ifndef TS_FILE_SERVE_H
#define TS_FILE_SERVE_H

#include "tinyserve.h"

/* Handle a file-serving request for the given client. */
void ts_file_serve(ts_client_t *client);

/* Detach any outstanding directory listing work from the client so that
 * its after_work callback drops the result instead of writing to the
 * (about-to-be-freed) connection. Safe to call from ts_client_close. */
void ts_file_serve_detach(ts_client_t *client);

#endif /* TS_FILE_SERVE_H */
