#!/usr/bin/env python3
"""Prefix borrow/return plan for the SM acceptance rounds.

The daily runner's PrefixLease moves a normally retired prefix from the session
that created it into the current session and moves it back on a normal
retirement. A borrowed session therefore ends with an empty profile/wine-prefix,
so later rounds must borrow from the original owner, never from the previous
borrower.

usage:
  prefix_plan.py plan --stamp 1029-1800 --plan "sm off sm"
  prefix_plan.py check-owner --home <NRBRIDGE_HOME> --session <owner>

plan prints one tab-separated line per round: index, kind, session, reuse.
check-owner exits 1 unless that session's prefix is initialized (system.reg).
"""
import argparse
import sys
from pathlib import Path

KINDS = ('sm', 'off')


def sessions(stamp: str, plan: list[str]) -> list[tuple[int, str, str]]:
    for kind in plan:
        if kind not in KINDS:
            raise SystemExit('unknown round kind: ' + kind)
    return [(index + 1, kind, f'sm-acc-{stamp}-{kind}{index + 1}')
            for index, kind in enumerate(plan)]


def reuse_for(rounds: list[tuple[int, str, str]], index: int) -> str | None:
    """The prefix owner is the first round of the batch; a borrowed prefix goes
    back there, so every later round borrows from it again."""
    if index == 0 or not rounds:
        return None
    return rounds[0][2]


def owner_initialized(home: Path, session: str) -> bool:
    prefix = home / 'runs' / session / 'profile' / 'wine-prefix'
    return (prefix / 'system.reg').is_file()


def main() -> None:
    parser = argparse.ArgumentParser()
    sub = parser.add_subparsers(dest='command', required=True)
    plan_p = sub.add_parser('plan')
    plan_p.add_argument('--stamp', required=True)
    plan_p.add_argument('--plan', required=True)
    check_p = sub.add_parser('check-owner')
    check_p.add_argument('--home', required=True)
    check_p.add_argument('--session', required=True)
    args = parser.parse_args()

    if args.command == 'plan':
        rounds = sessions(args.stamp, args.plan.split())
        for index, (_, kind, session) in enumerate(rounds):
            reuse = reuse_for(rounds, index) or '-'
            print(f'{index}\t{kind}\t{session}\t{reuse}')
        return
    if not owner_initialized(Path(args.home), args.session):
        print(f'owner prefix is not initialized: {args.session}', file=sys.stderr)
        raise SystemExit(1)


if __name__ == '__main__':
    main()
