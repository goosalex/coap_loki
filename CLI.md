
Configure the required Thread network parameters with the ``ot channel``, ``ot panid``, and ``ot networkkey`` commands.
Make sure to use the same parameters for all nodes that you add to the network.
The following example uses the default OpenThread parameters:

.. code-block:: console

   uart:~$ ot channel 11
   Done
   uart:~$ ot panid 0xabcd
   Done
   uart:~$ ot networkkey 00112233445566778899aabbccddeeff
   Done

Enable the Thread network with the ``ot ifconfig up`` and ``ot thread start`` commands:

.. code-block:: console

   uart:~$ ot ifconfig up
   Done
   uart:~$ ot thread start
   Done

ot channel 11
ot panid 0xabcd
ot networkkey 00112233445566778899aabbccddeeff
ot networkkey ff112233445566778899aabbccddee00

ot ifconfig up
ot thread start

Look for characteristic:




WINDOWS: 
Discover Border Routers:
> dns-sd -B _meshcop._udp local



QR Code:
v=1&&eui=(your_euid64)&&cc=(your_pass)
v=1&&eui=f4ce36ec34fd9eed&&cc=J01NME