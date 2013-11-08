#!/usr/bin/python

import json
import subprocess
import shlex
from StringIO import StringIO
import errno
import sys
import os
import io
import re

class UnexpectedReturn(Exception):
  def __init__(self, cmd, ret, expected, msg):
    if isinstance(cmd, list):
      self.cmd = ' '.join(cmd)
    else:
      assert isinstance(cmd, str) or isinstance(cmd, unicode), \
          'cmd needs to be either a list or a str'
      self.cmd = cmd
    self.cmd = str(self.cmd)
    self.ret = int(ret)
    self.expected = int(expected)
    self.msg = str(msg)

  def __str__(self):
    return repr('{c}: expected return {e}, got {r} ({o})'.format(
        c=self.cmd, e=self.expected, r=self.ret, o=self.msg))

def call(cmd):
  if isinstance(cmd, list):
    args = cmd
  elif isinstance(cmd, str) or isinstance(cmd, unicode):
    args = shlex.split(cmd)
  else:
    assert false, 'cmd is not a string/unicode nor a list!'

  print 'call: {0}'.format(args)
  proc = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
  ret = proc.wait()

  return (ret, proc)

def expect(cmd, expected_ret):

  try:
    (r, p) = call(cmd)
  except ValueError as e:
    print >> sys.stderr, \
             'unable to run {c}: {err}'.format(c=repr(cmd), err=e.message)
    return errno.EINVAL

  assert r == p.returncode, \
      'wth? r was supposed to match returncode!'

  if r != expected_ret:
    raise UnexpectedReturn(repr(cmd), r, expected_ret, str(p.stderr.read()))

  return p

def get_quorum_status(timeout=300):
  cmd = 'ceph quorum_status'
  if timeout > 0:
    cmd += ' --connect-timeout {0}'.format(timeout)

  p = expect(cmd, 0)
  j = json.loads(p.stdout.read())
  return j

def main():

  quorum_status = get_quorum_status()
  mon_names = [mon['name'] for mon in quorum_status['monmap']['mons']]

  print 'ping all monitors'
  for m in mon_names:
    print 'ping mon.{0}'.format(m)
    p = expect('ceph ping mon.{0}'.format(m), 0)
    reply = json.loads(p.stdout.read())

    assert reply['mon_status']['name'] == m, \
        'reply obtained from mon.{0}, expected mon.{1}'.format(
            reply['mon_status']['name'], m)

  print 'test out-of-quorum reply'
  for m in mon_names:
    print 'testing mon.{0}'.format(m)
    expect('ceph daemon mon.{0} quorum exit'.format(m), 0)

    quorum_status = get_quorum_status()
    assert m not in quorum_status['quorum_names'], \
        'mon.{0} was not supposed to be in quorum ({1})'.format(
            m, quorum_status['quorum_names'])

    p = expect('ceph ping mon.{0}'.format(m), 0)
    reply = json.loads(p.stdout.read())
    mon_status = reply['mon_status']

    assert mon_status['name'] == m, \
        'reply obtained from mon.{0}, expected mon.{1}'.format(
            mon_status['name'], m)

    assert mon_status['state'] == 'electing', \
        'mon.{0} is in state {1}, expected electing'.format(
            m,mon_status['state'])

    expect('ceph daemon mon.{0} quorum enter'.format(m), 0)

  print 'OK'
  return 0

if __name__ == '__main__':
  main()
