#include "daemon-server.h"
#include "daemon-log.h"
#include <syslog.h>
#include <assert.h>

struct backend {
	int id;
	void (*log)(log_state *s, void **state, int type, const char *message);
};

static void log_syslog(log_state *s, void **state, int type, const char *message)
{
	if (!*state) { /* initialize */
		*state = (void *)1;
		openlog(s->name, LOG_PID, LOG_DAEMON);
	}
	int prio = LOG_DEBUG;
	switch (type) {
	case DAEMON_LOG_INFO: prio = LOG_INFO; break;
	case DAEMON_LOG_WARN: prio = LOG_WARNING; break;
	case DAEMON_LOG_FATAL: prio = LOG_CRIT; break;
	}

	syslog(prio, "%s", message);
}

static void log_stderr(log_state *s, void **state, int type, const char *message)
{
	const char *prefix = "";
	switch (type) {
	case DAEMON_LOG_INFO: prefix = "I: "; break;
	case DAEMON_LOG_WARN: prefix = "W: " ; break;
	case DAEMON_LOG_ERROR:
	case DAEMON_LOG_FATAL: prefix = "E: " ; break;
	}

	fprintf(stderr, "%s%s\n", prefix, message);
}

struct backend backend[] = {
	{ DAEMON_LOG_OUTLET_SYSLOG, log_syslog },
	{ DAEMON_LOG_OUTLET_STDERR, log_stderr },
	{ 0, 0 }
};

void daemon_log(log_state *s, int type, const char *message) {
	int i = 0;
	while ( backend[i].id ) {
		if ( (s->log_config[type] & backend[i].id) == backend[i].id )
			backend[i].log( s, &s->backend_state[i], type, message );
		++ i;
	}
}

void daemon_logf(log_state *s, int type, const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	char *buf;
	if (dm_vasprintf(&buf, fmt, ap) < 0)
		return; /* _0 */
	daemon_log(s, type, buf);
	dm_free(buf);
}

struct log_line_baton {
	log_state *s;
	int type;
	const char *prefix;
};

static int _log_line(const char *line, void *baton) {
	struct log_line_baton *b = baton;
	daemon_logf(b->s, b->type, "%s%s", b->prefix, line);
	return 0;
}

void daemon_log_cft(log_state *s, int type, const char *prefix, const struct dm_config_node *n)
{
	struct log_line_baton b = { .s = s, .type = type, .prefix = prefix };
	dm_config_write_node(n, &_log_line, &b);
}

void daemon_log_multi(log_state *s, int type, const char *prefix, const char *msg)
{
	struct log_line_baton b = { .s = s, .type = type, .prefix = prefix };
	char *buf = dm_strdup(msg);
	char *pos = buf;

	if (!buf)
		return; /* _0 */

	while (pos) {
		char *next = strchr(pos, '\n');
		if (next)
			*next = 0;
		_log_line(pos, &b);
		pos = next ? next + 1 : 0;
	}
}

void daemon_log_enable(log_state *s, int outlet, int type, int enable)
{
	assert(type < 32);
	if (enable)
		s->log_config[type] |= outlet;
	else
		s->log_config[type] &= ~outlet;
}

static int _parse_one(log_state *s, int outlet, const char *type, int enable)
{
	int i;
	if (!strcmp(type, "all"))
		for (i = 0; i < 32; ++i)
			daemon_log_enable(s, outlet, i, enable);
	else if (!strcmp(type, "wire"))
		daemon_log_enable(s, outlet, DAEMON_LOG_WIRE, enable);
	else if (!strcmp(type, "debug"))
		daemon_log_enable(s, outlet, DAEMON_LOG_DEBUG, enable);
	else
		return 0;

	return 1;
}

int daemon_log_parse(log_state *s, int outlet, const char *types, int enable)
{
	char *buf = dm_strdup(types);
	char *pos = buf;

	if (!buf)
		return 0;

	while (pos) {
		char *next = strchr(pos, ',');
		if (next)
			*next = 0;
		if (!_parse_one(s, outlet, pos, enable)) {
			dm_free(buf);
			return 0;
		}
		pos = next ? next + 1 : 0;
	}

	dm_free(buf);

	return 1;
}