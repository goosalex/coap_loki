
API
|function|method|BLE|COAP|
|---|---|---|---|
|speed|PUT|x|x|
|speed|GET|x|x|
|speed|notify|x|-|
|direction|PUT|x|?|
|direction|GET|x|?|
|acceleration|PUT|x|T|
|acceleration|GET|x|T|
|stop|PUT|T|
|pwm|PUT|x|-|
|name|PUT|x|-| 
|join|PUT|x|N/A|


Next Todo: 
* ot ifconfig up, when joining
* why does the OT device not get a Globaly routable Prefix ?

* Error when changing Names:
```
[00:05:08.079,772] <inf> loki_main: Set new advertised name: Loko

Advertising successfully started
[00:05:08.081,268] <inf> loki_main: Stoppping advertising

[00:05:08.081,298] <inf> loki_main: Starting advertising again

[00:05:08.082,733] <err> loki_main: Error saving advertised names: -11
```
