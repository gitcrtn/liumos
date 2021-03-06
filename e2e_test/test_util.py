#!/usr/bin/env python3
import pexpect
import os
import sys

liumos_root_path = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
print('liumOS root is at: ', liumos_root_path, file=sys.stderr, flush=True)

def launch_qemu():
    p = pexpect.spawn("make -C {} run_for_e2e_test".format(liumos_root_path))
    p.expect(r"\(qemu\)")
    print('QEMU Launched', file=sys.stderr, flush=True)
    return p

def connect_to_liumos_serial():
    p = pexpect.spawn("telnet localhost 1235")
    p.expect(r"\(liumos\)")
    print('Reached to liumos prompt via serial', file=sys.stderr, flush=True)
    return p

def connect_to_liumos_builder():
    p = pexpect.spawn("docker exec -it liumos-builder0 /bin/bash")
    p.expect(r"\(liumos-builder\)")
    print('Reached to liumos-builder on docker', file=sys.stderr, flush=True)
    return p

def launch_liumos_on_docker():
    # returns connection to qemu monitor
    p = pexpect.spawn("make run_docker")
    p.expect(r"\(qemu\)")
    print('Reached to qemu monitor on docker', file=sys.stderr, flush=True)
    return p

def just_print_everything(p): # For debugging
    while True:
        try:
            print(p.readline())
        except pexpect.EOF:
            print("Got EOF.")
            print(p.before)
            sys.exit(1)

def expect_liumos_command_result(p, cmd, expected, timeout):
    p.sendline(cmd);
    try:
        p.expect(expected, timeout=timeout)
    except pexpect.TIMEOUT:
        print("FAIL (timed out): {} => {}".format(cmd, expected))
        print(p.before)
        sys.exit(1)
    print("PASS: {} => {}".format(cmd, expected))

