.. _bluetooth-dev:

Developing Bluetooth Applications
#################################

Bluetooth applications are developed using the common infrastructure and
approach that is described in the :ref:`application` section of the
documentation.

Additional information that is only relevant to Bluetooth applications can be
found in this page.

Thread safety
*************

Calling into the Bluetooth API is intended to be thread safe, unless otherwise
noted in the documentation of the API function. The effort to ensure that this
is the case for all API calls is an ongoing one, but the overall goal is
formally stated in this paragraph. Bug reports and Pull Requests that move the
subsystem in the direction of such goal are welcome.

Q & A
*****


= Event loop vs events and callbacks.

The Bluetooth host has to talk HCI.

HCI RX path has
 - Blocking: no flow control "events", and ISO data
  - an idle controller produces no events.
  - each normal command produces exactly one status.
   - and causes zero or more events to be sent
   - a 'command complete' is a fusion of a status and a data
     event.
   - ISO has dedicated flow control. This is fine, since ISO is
    supposed to be a real-time transport. Users of the ISO APIs
    must be aware that they are stalling the RX path.
 - Non-blocking, blocking cancelable: ACL data.
  - normally have flow control but it's broken. The controller
    gives itself credits by disconnecting.
   - this means data that is already received must be up for
     discarding if the host needs to continue reading from HCI.
   - there is no way to specify host buffers per connection. so
     when a connection drops, the controller can immediately use
     the credits it has given itself for the other connection.
   - the disconnect event is a blocking cancellation of the acl
     rx data. The application may still choose to process it,
     but while it's doing so, it is blocking HCI.

Blocking HCI of course implies blocking ISO data, which likely
is real-time sensitive. It's therefore imperative that the
cancellation if processed quickly.

The HCI TX path is dependent on the RX path because

SDC quirk: no ACL cancellation: The SDC will delay the
disconnected event until the host has freed all buffers
associated. Can this cause troubles? Forms a dependency from the
disconnect to the processing of all data. If the processing of
the data is dependent on the disconnect, we have formed a
deadlock.

== What is a Bluetooth callback?

In the Zephyr community, all dependency injection is termed a
'callback'.

Synchronous callbacks. E.g. `bt_conn_foreach`. This function
takes a function pointer as an argument and repeatedly invokes
that function.

Asynchronous callbacks: E.g. `bt_gatt_write`. `func` is invoked
when the params pointer is no longer in use. It also serves
double-duty and delivers a status code. Since this `func`
signals the end of the operation started by `bt_gatt_write` and
`func` was registered on that operation.

Some callback parameters are accompanied by a `user_data`
parameter. This parameter value is passed as-is. It carries not
meaning to the function that accepts the callback. Instead, it
allows some extra data to be attached to the function pointer.
Mathematically this is an approximation to higher
order-functions. It allows the callback to compose.

All function pointer may be informally referred to as
'callback' in the Zephyr community.

There are two concepts that are both called 'callbacks' in the
  context of the Zephyr Bluetooth subsystem:

 - Registered callbacks: These are functions that are registered
   with the Bluetooth subsystem to be called when a specific
   event occurs. For example, the :c:func:`bt_conn_cb_register`
   function is used to register a callback that is called when a
   connection is established or disconnected.

   You might recognize these by the terms 'Event listeners',
   'Event handlers' or 'Interrupt handlers'.

   There should usually be a corresponding function to
   unregister the callback.

   Disabling the Bluetooth system shall not clear the
   registered callbacks. They can be though of as external
   observers of the singleton object Bluetooth subsystem, and
   existing outside of it.

 - Callback arguments: These are passed to Bluetooth API that
   cause some effect.
   These callbacks are always invoked exactly once. The
   invocation signals that the operation is retired, either
   successfully completed or failed or aborted.
   They are often used to inform the caller of the result of an
   operation.

Q: What types of operations can I do in a callback?

It is important to understand that all code that runs occupies
some execution context, that is usually a thread or an
interrupt.

Furthermore, reliable delivery guarantees are fundamentally
propagating blocking of the consumers execution context to the
producer execution context.

The callbacks in Bluetooth give delivery guarantees, as expected
by the intuitive developer; event listeners will never miss an
event. In fact, the events will always be delivered in the order
they occurred.

But this means on a fundamental level that blocking in a
Bluetooth callback will block subsequent events from being
generated. I.e. blocking will eventually propagate to the HCI
driver and we will stop reading out any messages from the
controller. This includes control-flow messages from the
controller, which means the blocking propagates to the HCI TX
path as well.

So you must assume that the Bluetooth subsystem can be
completely stalled while you while you are in a Bluetooth
callback. And so, you must never wait for anything that directly
or indirectly waits on the Bluetooth subsystem in a Bluetooth
callback.

The Zephyr Bluetooth subsystem is has a lot of various buffers
that will delay the stall of the subsystem, but it offers no
guarantees. If the application developer want to depend on this,
it is up to the application developer to do the heavy analysis
of the data paths, buffer size, throughput, and their
statistical distributions. Ald these can change between Zephyr
versions. Or the application developer can just order more RAM
to the point that it ought to be enough, like in a desktop
computer OS.

While in a Bluetooth callback, assume you may accidentally be
holding a lock on the following:
 - The system-wide work-queue.
 - The Bluetooth HCI RX path.
 - The Bluetooth HCI TX path.

Q: What happens if I block in a Bluetooth callback?

Blocking in a Bluetooth callback may pause all HCI transport in
both directions, and it may be blocking the Zephyr system work
queue.

The above rule applies to all of Zephyr host's callbacks that do
not specify otherwise.

Q: Is it okay to call the Bluetooth API in a callback?

Short answer is 'no', there is no guarantee calling arbitrary
Bluetooth API in a callback will work. But, if you configure the
relevant APIs to have plenty of buffers and test it thoroughly,
it might work for you.

This is because a lot of Bluetooth API is blocking on the same
resources that an active callback holds. This forms a dead-lock.

You can of-course invoke any Bluetooth API marked ISR-safe.

Q: How much stack space is available in Bluetooth callbacks?

There are no guarantees about stack space.

Q: How can I get more stack space in the Bluetooth callbacks?

Stack size are often selected by some kconfig, and you can
override those. The following are some configs that may play a
role. This is not an exhaustive list.

 - CONFIG_BT_RX_STACK_SIZE
 - CONFIG_SYSTEM_WORKQUEUE_STACK_SIZE

Some callbacks are invoked on arbitrary threads. There is no
feasible way of controlling this. E.g. calling into
`bt_conn_unref` may involve running callbacks.

Q: Do all the BT callbacks behave the same way and have same
properties?

There are no such guarantees. In fact, there is no guarantee
that the same callback will have the same properties between
invocations.

For illustration, each invocation may be
 - on a different thread
  - including but not limited to the BT threads
  - may be some thread that called into the BT subsystem
   - e.g. `bt_conn_unref`
 - or a different work queue
  - including the system work-queue
 - with a different priority
 - with a different preempt/cooperative status
 - with a different stack size
 - with a different stack use

Q: I found a sample that calls the Bluetooth API in a callback,
   is that a bug?

Yes, it is a bug. Please report it. Yes, this is historically
common, but we want to remedy it. PRs with fixes for this are
welcome.

Q: I though callbacks and asynchronous APIs would make
developing easer than a event queue would, not harder. Is it
possible to fix these issues with callbacks?

The problem is the callbacks are not separaed will enough from
the HCI event loop. They are just a thin wrapper.

What do I mean by separation? To obtain separation, there has to
be a summarization between HCI and the callbacks. This could be
a 'virtual' representation of the controller. The host processes
events from the controller quickly, folding it into the
representation. Because we are limited to a fixed amount of
memory, the representation must inherently be lossy with regards
to the event history. But the representation can accurately
depict one state of the controller.

To exemplify the above, we cannot store a queue of connected /
disconnected events if we treat them as a 'general event'. Only
by immediately parsing the event, understanding that it's e.g. a
disconnection, and updating the depiction of the controller; by
marking a connection as disconnected.

We can mark a connection as disconnected because there is a
specific memory allocation for a connection that was previously
made. This is the bt_conn object in Zephyr for example.

The application can register callbacks, but how do we know when
to invoke them? Consider is as a interrupt flag. The application
has to provide a memory to store the flag. The flag can be
complex. In the case of disconnects, we can have the application
provide an array flags, with one flag for each connection slot.
Then we have a memory location, a register in RTL, to remember
the need to invoke.

Any time you don't provide a memory, it will necessarily take
and hold onto some other resource in exchange. The information
has to go somewhere. This somewhere may be the stack of the
Bluetooth RX thread. But this thread is intertwined with the HCI
transport, so we have lost our separation.

Rule of thumb:
Callbacks registration must provide memory, not just for storing
the pointer and user data, but also for the request flag.

.. _bluetooth-hw-setup:

Hardware setup
**************

This section describes the options you have when building and debugging Bluetooth
applications with Zephyr. Depending on the hardware that is available to you,
the requirements you have and the type of development you prefer you may pick
one or another setup to match your needs.

There are 4 possible hardware setups to use with Zephyr and Bluetooth:

#. Embedded
#. QEMU with an external Controller
#. :ref:`native_sim <native_sim>` with an external Controller
#. Simulated nRF5x with BabbleSim

Embedded
========

This setup relies on all software running directly on the embedded platform(s)
that the application is targeting.
All the :ref:`bluetooth-configs` and :ref:`bluetooth-build-types` are supported
but you might need to build Zephyr more than once if you are using a dual-chip
configuration or if you have multiple cores in your SoC each running a different
build type (e.g., one running the Host, the other the Controller).

To start developing using this setup follow the :ref:`Getting Started Guide
<getting_started>`, choose one (or more if you are using a dual-chip solution)
boards that support Bluetooth and then :ref:`run the application
<application_run_board>`).

.. _bluetooth-hci-tracing:

Embedded HCI tracing
--------------------

When running both Host and Controller in actual Integrated Circuits, you will
only see normal log messages on the console by default, without any way of
accessing the HCI traffic between the Host and the Controller.  However, there
is a special Bluetooth logging mode that converts the console to use a binary
protocol that interleaves both normal log messages as well as the HCI traffic.
Set the following Kconfig options to enable this protocol before building your
application:

.. code-block:: console

   CONFIG_BT_DEBUG_MONITOR_UART=y
   CONFIG_UART_CONSOLE=n

Setting :kconfig:option:`CONFIG_BT_DEBUG_MONITOR_UART` to ``y`` replaces the
:kconfig:option:`CONFIG_BT_DEBUG_LOG` option, and setting :kconfig:option:`CONFIG_UART_CONSOLE`
to ``n`` disables the default ``printk``/``printf`` hooks.

To decode the binary protocol that will now be sent to the console UART you need
to use the btmon tool from :ref:`BlueZ <bluetooth_bluez>`:

.. code-block:: console

   $ btmon --tty <console TTY> --tty-speed 115200

Host on Linux with an external Controller
=========================================

.. note::
   This is currently only available on GNU/Linux

This setup relies on a "dual-chip" :ref:`configuration <bluetooth-configs>`
which is comprised of the following devices:

#. A :ref:`Host-only <bluetooth-build-types>` application running in the
   :ref:`QEMU <application_run_qemu>` emulator or the :ref:`native_sim <native_sim>` native
   port of Zephyr
#. A Controller, which can be one of the following types:

   * A commercially available Controller
   * A :ref:`Controller-only <bluetooth-build-types>` build of Zephyr
   * A Virtual controller

.. warning::
   Certain external Controllers are either unable to accept the Host to
   Controller flow control parameters that Zephyr sets by default (Qualcomm), or
   do not transmit any data from the Controller to the Host (Realtek). If you
   see a message similar to::

     <wrn> bt_hci_core: opcode 0x0c33 status 0x12

   when booting your sample of choice (make sure you have enabled
   :kconfig:option:`CONFIG_LOG` in your :file:`prj.conf` before running the
   sample), or if there is no data flowing from the Controller to the Host, then
   you need to disable Host to Controller flow control. To do so, set
   ``CONFIG_BT_HCI_ACL_FLOW_CONTROL=n`` in your :file:`prj.conf`.

QEMU
----

You can run the Zephyr Host on the :ref:`QEMU emulator<application_run_qemu>`
and have it interact with a physical external Bluetooth Controller.
Refer to :ref:`bluetooth_qemu_native` for full instructions on how to build and
run an application in this setup.

native_sim
----------

.. note::
   This is currently only available on GNU/Linux

The :ref:`native_sim <native_sim>` target builds your Zephyr application
with the Zephyr kernel, and some minimal HW emulation as a native Linux
executable.
This executable is a normal Linux program, which can be debugged and
instrumented like any other, and it communicates with a physical or virtual
external Controller.

Refer to :ref:`bluetooth_qemu_native` for full instructions on how to build and
run an application with a physical controller. For the virtual controller refer
to :ref:`bluetooth_virtual_posix`.

Simulated nRF5x with BabbleSim
==============================

.. note::
   This is currently only available on GNU/Linux

The :ref:`nrf52_bsim <nrf52_bsim>` and :ref:`nrf5340bsim <nrf5340bsim>` boards,
are simulated target boards
which emulate the necessary peripherals of a nRF52/53 SOC to be able to develop
and test BLE applications.
These boards, use:

   * `BabbleSim`_ to simulate the nRF5x modem and the radio environment.
   * The POSIX arch and native simulator to emulate the processor, and run natively on your host.
   * `Models of the nrf5x HW <https://github.com/BabbleSim/ext_NRF_hw_models/>`_

Just like with the :ref:`native_sim <native_sim>` target, the build result is a normal Linux
executable.
You can find more information on how to run simulations with one or several
devices in either of :ref:`these boards's documentation <nrf52bsim_build_and_run>`.

With the :ref:`nrf52_bsim <nrf52_bsim>`, typically you do :ref:`Combined builds
<bluetooth-build-types>`, but it is also possible to build the controller with one of the
:ref:`HCI UART <bluetooth-hci-uart-sample>` samples in one simulated device, and the host with
the H4 driver instead of the integrated controller in another simulated device.

With the :ref:`nrf5340bsim <nrf5340bsim>`, you can build with either, both controller and host
on its network core, or, with the network core running only the controller, the application
core running the host and your application, and the HCI transport over IPC.

Initialization
**************

The Bluetooth subsystem is initialized using the :c:func:`bt_enable`
function. The caller should ensure that function succeeds by checking
the return code for errors. If a function pointer is passed to
:c:func:`bt_enable`, the initialization happens asynchronously, and the
completion is notified through the given function.

Bluetooth Application Example
*****************************

A simple Bluetooth beacon application is shown below. The application
initializes the Bluetooth Subsystem and enables non-connectable
advertising, effectively acting as a Bluetooth Low Energy broadcaster.

.. literalinclude:: ../../../samples/bluetooth/beacon/src/main.c
   :language: c
   :lines: 19-
   :linenos:

The key APIs employed by the beacon sample are :c:func:`bt_enable`
that's used to initialize Bluetooth and then :c:func:`bt_le_adv_start`
that's used to start advertising a specific combination of advertising
and scan response data.

.. _BabbleSim: https://babblesim.github.io/
