CC = gcc
LIBS = -lvmi -lm
OPENSSL_LIBS = -lcrypto

RM = rm -rf

MAIN = attestation_kernel
VMI = vmi
SYSCALL_CHECK = vmi-syscall-integrity
IDT_CHECK = vmi-idt-integrity
NETWORK_CHECK = vmi-procfs-integrity
PROCESS_LIST = vmi-process-list
PROCESS_KILL = vmi-process-kill
PROCESS_BLOCK = vmi-process-block
DRIVERAPI_TRACE = vmi-driver-trace
SOCKETAPI_TRACE = vmi-socket-trace


OBJECTS = $(MAIN).o $(PROCESS_LIST).o $(SYSCALL_CHECK).o  $(IDT_CHECK).o \
	$(NETWORK_CHECK).o $(SOCKETAPI_TRACE).o $(DRIVERAPI_TRACE).o \
	$(PROCESS_BLOCK).o $(PROCESS_KILL).o

$(MAIN): $(OBJECTS)
	$(CC) -o $(MAIN) $(OBJECTS) $(LIBS) $(OPENSSL_LIBS)

$(OBJECTS): $(VMI).h

.PHONE: clean
clean:
	$(RM) $(MAIN) $(OBJECTS)
