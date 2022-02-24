#include <ngx_conf_file.h>
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <ngx_string.h>

#include <fcntl.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

extern ngx_module_t ngx_logurl_module;

typedef struct {
  ngx_flag_t enable;
  ngx_str_t host;
  ngx_uint_t port;
  ngx_str_t baseurl;
  ngx_uint_t requestTimeoutSec;
} ngx_logurl_conf_t;

static void *ngx_logurl_create_loc_conf(ngx_conf_t *cf) {
  ngx_logurl_conf_t *conf;

  conf = ngx_pcalloc(cf->pool, sizeof(*conf));
  if (conf == NULL) {
    return NULL;
  }

  conf->enable = NGX_CONF_UNSET;
  conf->port = NGX_CONF_UNSET_UINT;
  conf->requestTimeoutSec = NGX_CONF_UNSET_UINT;

  return conf;
}

static ngx_command_t ngx_logurl_commands[] = {
    {ngx_string("logurl_enable"),
     NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_CONF_FLAG,
     ngx_conf_set_flag_slot, NGX_HTTP_LOC_CONF_OFFSET,
     offsetof(ngx_logurl_conf_t, enable), NULL},

    {ngx_string("logurl_host"),
     NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF |
         NGX_CONF_TAKE1,
     ngx_conf_set_str_slot, NGX_HTTP_LOC_CONF_OFFSET,
     offsetof(ngx_logurl_conf_t, host), NULL},

    {ngx_string("logurl_port"),
     NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF |
         NGX_CONF_TAKE1,
     ngx_conf_set_num_slot, NGX_HTTP_LOC_CONF_OFFSET,
     offsetof(ngx_logurl_conf_t, port), NULL},

    {ngx_string("logurl_baseurl"),
     NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF |
         NGX_CONF_TAKE1,
     ngx_conf_set_str_slot, NGX_HTTP_LOC_CONF_OFFSET,
     offsetof(ngx_logurl_conf_t, baseurl), NULL},

    {ngx_string("logurl_request_timeout"),
     NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF |
         NGX_CONF_TAKE1,
     ngx_conf_set_num_slot, NGX_HTTP_LOC_CONF_OFFSET,
     offsetof(ngx_logurl_conf_t, requestTimeoutSec), NULL},

    ngx_null_command};

static char *ngx_logurl_merge_loc_conf(ngx_conf_t *cf, void *parent,
                                       void *child) {
  ngx_logurl_conf_t *prev = parent;
  ngx_logurl_conf_t *conf = child;

  ngx_conf_merge_value(conf->enable, prev->enable, 0);
  ngx_conf_merge_str_value(conf->host, prev->host, "myhttpserver");
  ngx_conf_merge_uint_value(conf->port, prev->port, 8080);
  ngx_conf_merge_str_value(conf->baseurl, prev->baseurl, "/fileevent/put");
  ngx_conf_merge_uint_value(conf->requestTimeoutSec, prev->requestTimeoutSec,
                            30);

  return NGX_CONF_OK;
}

static ngx_int_t ngx_logurl_put(ngx_http_request_t *r) {
  ngx_logurl_conf_t *cfg = ngx_http_get_module_loc_conf(r, ngx_logurl_module);
  if (!cfg->enable) {
    ngx_log_debug(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                  "logurl: disabled");
    return NGX_OK;
  }

  ngx_log_debug(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "logurl: status: %i",
                r->err_status);

  if (r->err_status - NGX_HTTP_OK > 1) {
    ngx_log_debug(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                  "logurl: don't inform on error");
    return NGX_OK;
  }

  if (!r->valid_unparsed_uri) {
    ngx_log_error(NGX_LOG_ALERT, r->connection->log, 0,
                  "logurl: did not get a valid uri");
    return NGX_ERROR;
  }

  ngx_log_debug(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                "logurl: unparsed_uri %V", r->unparsed_uri);

  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;

  const int port = cfg->port;

  ngx_log_debug(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                "logurl: %V:%d at %V", &cfg->host, port, &cfg->baseurl);

  u_char port_string[8];
  ngx_sprintf(port_string, "%i", port);

  struct addrinfo *results = NULL;
  const int error_num =
      getaddrinfo((const char *)cfg->host.data, (const char *)port_string,
                  &hints, &results);
  if (error_num != 0) {
    ngx_log_error(NGX_LOG_ALERT, r->connection->log, 0,
                  "logurl: getaddrinfo failed %s", gai_strerror(error_num));
    return NGX_ERROR;
  }

  ngx_log_debug(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                "logurl: getaddrinfo successful");

  ngx_socket_t s = (ngx_socket_t)-1;

  for (struct addrinfo *cur_addr = results;
       s == (ngx_socket_t)-1 && cur_addr != NULL;
       cur_addr = cur_addr->ai_next) {
    s = ngx_socket(cur_addr->ai_family, cur_addr->ai_socktype,
                   cur_addr->ai_protocol);
    if (s != (ngx_socket_t)-1) {
      ngx_log_debug(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                    "logurl: connect");
      int rc = connect(s, cur_addr->ai_addr, cur_addr->ai_addrlen);
      if (rc == -1) {
        int err = ngx_socket_errno;
        ngx_log_debug(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                      "logurl: connect failed %d", err);
        if (err != NGX_EINPROGRESS) {
          ngx_log_error(NGX_LOG_ALERT, r->connection->log, ngx_socket_errno,
                        "logurl: connect() to %V failed", &cfg->host);
          if (ngx_close_socket(s) == -1) {
            ngx_log_error(NGX_LOG_ALERT, r->connection->log, ngx_socket_errno,
                          "logurl: " ngx_close_socket_n " failed");
          }
          freeaddrinfo(results);
          return NGX_ERROR;
        }
      }
    }
  }
  ngx_log_debug(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                "logurl: connect success");
  freeaddrinfo(results);
  ngx_log_debug(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                "logurl: connect success 2");

  if (s == (ngx_socket_t)-1) {
    ngx_log_error(NGX_LOG_ALERT, r->connection->log, ngx_socket_errno,
                  "logurl: " ngx_socket_n " failed");
    return NGX_ERROR;
  }

  char buf[4096];
  snprintf(buf, sizeof(buf),
           "GET http://%s%s HTTP/1.1\r\n"
           "Host: %s\r\n"
           "\r\n",
           (const char *)cfg->baseurl.data, (const char *)r->uri.data,
           (const char *)cfg->host.data);

  ngx_log_debug(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                "logurl: request: '%s'", buf);

  size_t sent = 0u;
  while (sent < strlen(buf)) {
    const int ret = send(s, buf + sent, strlen(buf) - sent, 0);
    if (ret < 0) {
      ngx_log_error(NGX_LOG_ALERT, r->connection->log, ngx_socket_errno,
                    "logurl: send() failed");
      if (ngx_close_socket(s) == -1) {
        ngx_log_error(NGX_LOG_ALERT, r->connection->log, ngx_socket_errno,
                      "logurl: " ngx_close_socket_n " failed");
      }
      return NGX_ERROR;
    }
    sent += ret;
  }

  struct timeval tv;
  tv.tv_sec = cfg->requestTimeoutSec;
  tv.tv_usec = 0;
  if (tv.tv_sec > 0) {
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(tv));
  }

  u_char recvBuf[1024];
  const int32_t recvLen = recv(s, (char *)recvBuf, sizeof(recvBuf), 0);
  if (recvLen < 0) {
    ngx_log_error(NGX_LOG_ALERT, r->connection->log, ngx_socket_errno,
                  "logurl: recv() failed");
    return NGX_ERROR;
  }

  if (ngx_close_socket(s) == -1) {
    ngx_log_error(NGX_LOG_ALERT, r->connection->log, ngx_socket_errno,
                  "logurl: " ngx_close_socket_n " failed");
  }
  ngx_log_debug(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                "logurl: %V:%d at %V", &cfg->host, port, &cfg->baseurl);
  return NGX_OK;
}

static ngx_int_t ngx_logurl_handler(ngx_http_request_t *r) {
  if (r->method == NGX_HTTP_PUT) {
    return ngx_logurl_put(r);
  }
  return NGX_OK;
}

static ngx_int_t ngx_logurl_init(ngx_conf_t *cf) {
  ngx_http_core_main_conf_t *cmcf;
  ngx_http_handler_pt *h;

  cmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_core_module);
  h = ngx_array_push(&cmcf->phases[NGX_HTTP_LOG_PHASE].handlers);
  if (h == NULL) {
    ngx_log_debug(NGX_LOG_DEBUG_HTTP, cf->log, 0, "ngx_logurl_init failed");
    return NGX_ERROR;
  }
  ngx_log_debug(NGX_LOG_DEBUG_HTTP, cf->log, 0, "ngx_logurl_init successful");
  *h = ngx_logurl_handler;
  return NGX_OK;
}

static ngx_http_module_t ngx_logurl_module_ctx = {
    NULL,            /* preconfiguration */
    ngx_logurl_init, /* postconfiguration */

    NULL, /* create main configuration */
    NULL, /* init main configuration */

    NULL, /* create server configuration */
    NULL, /* merge server configuration */

    ngx_logurl_create_loc_conf, /* create location configuration */
    ngx_logurl_merge_loc_conf   /* merge location configuration */
};

ngx_module_t ngx_logurl_module = {NGX_MODULE_V1,
                                  &ngx_logurl_module_ctx, /* module context */
                                  ngx_logurl_commands, /* module directives */
                                  NGX_HTTP_MODULE,     /* module type */
                                  NULL,                /* init master */
                                  NULL,                /* init module */
                                  NULL,                /* init process */
                                  NULL,                /* init thread */
                                  NULL,                /* exit thread */
                                  NULL,                /* exit process */
                                  NULL,                /* exit master */
                                  NGX_MODULE_V1_PADDING};
