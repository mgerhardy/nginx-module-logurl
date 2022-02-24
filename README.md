# About

Call an url if a put request is finished (no TLS supported!!)

# Configuration via http, server or location

* `logurl_baseurl`: `/fileevent/put` the path of the file is appended here (url encoded)
* `logurl_port`: `8080`
* `logurl_host`: `myhttpserver`
* `logurl_request_timeout`: timeout in seconds
* `logurl_enable`: will disable the log handler and event calling completly if set to `off`

# Example config

```
location / {
    [...]
    logurl_enable        on;
    logurl_host          myhttpserver;
    logurl_baseurl       /fileevent/put;
    logurl_port          8080;
    logurl_request_timeout 1;
    [...]
}
```

# Compile and test

> Execute this to compile it:

```sh
curl https://nginx.org/download/nginx-1.20.2.tar.gz -o nginx-1.20.2.tar.gz
tar -xzf nginx-1.20.2.tar.gz
cd nginx-1.20.2
./configure --with-debug --add-module=../nginx-module-logurl
```

This should allow you to properly develop in your IDE. But I would suggest to
use the provided docker-compose file to run the plugin in its usual environment.
