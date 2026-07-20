"""Tests for truenas_bootenv.cli (pure Python; no root or pools).

The engine calls are monkeypatched, so these tests exercise argument
parsing, dataset-path construction, error presentation and exit codes.
The first test pins the load-bearing packaging rule: importing the CLI
module must never import truenas_pylibzfs, because argparse-manpage
imports it in the Debian build chroot where no ZFS kernel module is
loaded.
"""

import pytest
import subprocess
import sys
import time
from truenas_bootenv import cli, engine
from truenas_bootenv.errors import BEBusy, BEError, BENotFound


def test_cli_import_never_imports_the_binding():
    # exit 2 = leak, other nonzero = import failed for another reason,
    # so the assertion message cannot blame the wrong cause
    code = (
        'import sys; import truenas_bootenv.cli; '
        'sys.exit(2 if "truenas_pylibzfs" in sys.modules else 0)'
    )
    r = subprocess.run(
        [sys.executable, '-c', code], capture_output=True, text=True,
    )
    assert r.returncode != 2, 'importing cli pulled in truenas_pylibzfs'
    assert r.returncode == 0, f'cli import failed: {r.stderr}'


def test_build_parser_lists_all_verbs():
    parser = cli.build_parser()
    assert parser.prog == 'truenas-bootenv'
    # every verb the manpage must document
    text = parser.format_help()
    for verb in ('list', 'activate', 'create', 'destroy'):
        assert verb in text


@pytest.fixture
def fake_env(monkeypatch):
    """Fake handle/pool/running-BE plumbing and record engine calls."""
    calls = []
    monkeypatch.setattr(cli, '_open_handle', lambda: 'LZH')
    monkeypatch.setattr(cli, '_resolve_pool', lambda lzh, arg: arg or 'bp')
    monkeypatch.setattr(engine, 'running_dataset',
                        lambda: 'bp/ROOT/current')
    monkeypatch.setattr(
        engine, 'activate',
        lambda lzh, *, dataset, run_grub: calls.append(
            ('activate', dataset, run_grub)),
    )
    monkeypatch.setattr(
        engine, 'create',
        lambda lzh, *, source_dataset, target_dataset, run_grub:
            calls.append(('create', source_dataset, target_dataset,
                          run_grub)),
    )
    monkeypatch.setattr(
        engine, 'destroy',
        lambda lzh, *, dataset, running_ds, run_grub: calls.append(
            ('destroy', dataset, running_ds, run_grub)),
    )
    monkeypatch.setattr(engine, 'dataset_exists', lambda lzh, name: True)
    return calls


def test_activate_builds_full_dataset_path(fake_env):
    assert cli.main(['activate', 'be1']) == 0
    assert fake_env == [('activate', 'bp/ROOT/be1', True)]


def test_create_defaults_source_to_running(fake_env):
    assert cli.main(['create', 'newbe']) == 0
    assert fake_env == [('create', 'bp/ROOT/current', 'bp/ROOT/newbe', True)]


def test_create_with_explicit_source(fake_env):
    assert cli.main(['create', '-e', 'golden', 'newbe']) == 0
    assert fake_env == [('create', 'bp/ROOT/golden', 'bp/ROOT/newbe', True)]


def test_destroy_passes_running_guard(fake_env):
    assert cli.main(['destroy', 'oldbe']) == 0
    assert fake_env == [('destroy', 'bp/ROOT/oldbe', 'bp/ROOT/current', True)]


def test_destroy_of_a_zfs_invalid_name_is_a_clean_error(
        fake_env, monkeypatch, capsys):
    # destroy composes the path without judging the name's characters, so
    # a name libzfs itself rejects reaches the binding. dataset_exists
    # translates that to a BEError, which main() turns into one line on
    # stderr and exit 1 -- never a traceback
    def invalid(lzh, name):
        raise BEError(f'ZFS error: invalid character in {name!r}')

    monkeypatch.setattr(engine, 'dataset_exists', invalid)
    assert cli.main(['destroy', 'foo*bar']) == 1
    assert fake_env == []
    captured = capsys.readouterr()
    assert captured.err.startswith('truenas-bootenv: ')
    assert 'Traceback' not in captured.err


def test_destroy_reaches_a_name_create_would_refuse(fake_env):
    # list shows boot environments inherited from a release that allowed
    # interior spaces, so destroy must be able to reach them: it validates
    # the dataset's position, not the name's characters. create/activate
    # stay strict
    assert cli.main(['destroy', 'old be']) == 0
    assert fake_env == [('destroy', 'bp/ROOT/old be', 'bp/ROOT/current',
                         True)]


def test_create_still_refuses_a_space_named_target(fake_env, capsys):
    assert cli.main(['create', 'bad name']) == 1
    assert fake_env == []
    assert 'invalid boot environment name' in capsys.readouterr().err


def test_destroy_of_absent_be_reports_not_found(fake_env, monkeypatch, capsys):
    # the engine counts an absent boot environment as success (the
    # updater's prune loop retries after a crash), but a person at a
    # shell must not be told that something which never existed was
    # destroyed. zectl exits non-zero here too
    monkeypatch.setattr(engine, 'dataset_exists', lambda lzh, name: False)
    assert cli.main(['destroy', 'ghost']) == 1
    assert fake_env == []                       # engine.destroy never ran
    captured = capsys.readouterr()               # drains both streams once
    assert 'not found' in captured.err
    assert 'destroyed' not in captured.out


def test_pool_flag_overrides_detection(fake_env):
    assert cli.main(['--pool', 'tank', 'activate', 'be1']) == 0
    assert fake_env == [('activate', 'tank/ROOT/be1', True)]


def test_invalid_name_refused_before_any_engine_call(fake_env, capsys):
    assert cli.main(['activate', 'bad@name']) == 1
    assert fake_env == []
    assert 'invalid boot environment name' in capsys.readouterr().err


def test_engine_error_becomes_message_and_exit_1(fake_env, monkeypatch,
                                                 capsys):
    def refuse(lzh, *, dataset, running_ds, run_grub):
        raise BEBusy(f'{dataset!r} is the running boot environment')

    monkeypatch.setattr(engine, 'destroy', refuse)
    assert cli.main(['destroy', 'current']) == 1
    err = capsys.readouterr().err
    assert 'truenas-bootenv:' in err and 'running boot environment' in err


def test_not_found_error_is_presented(fake_env, monkeypatch, capsys):
    def missing(lzh, *, dataset, run_grub):
        raise BENotFound(f'{dataset!r} not found')

    monkeypatch.setattr(engine, 'activate', missing)
    assert cli.main(['activate', 'ghost']) == 1
    assert 'not found' in capsys.readouterr().err


def _entry(name, created, active=False, activated=False):
    # the real dataclass, so a field rename breaks these tests too
    return engine.BootEnvironment(
        name=name, dataset=f'bp/ROOT/{name}', created=created,
        used_bytes=4096, keep=None, can_activate=True,
        active=active, activated=activated,
    )


@pytest.fixture
def fake_list_env(monkeypatch):
    entries = [
        _entry('one', 1_750_000_000, active=True),
        _entry('two', 1_760_000_000, activated=True),
        _entry('three', 1_770_000_000),
    ]
    monkeypatch.setattr(cli, '_open_handle', lambda: 'LZH')
    monkeypatch.setattr(cli, '_resolve_pool', lambda lzh, arg: 'bp')
    monkeypatch.setattr(
        engine, 'list_environments',
        lambda lzh, *, pool_name, running_ds: entries,
    )
    return entries


def test_list_flags_running_and_activated(fake_list_env, capsys):
    assert cli.main(['list']) == 0
    out = capsys.readouterr().out
    lines = [line.split() for line in out.splitlines()]
    assert lines[0][:2] == ['Name', 'Active']
    assert ['one', 'N'] == lines[1][:2]
    assert ['two', 'R'] == lines[2][:2]


def test_list_scripting_mode_has_no_header(fake_list_env, capsys):
    assert cli.main(['list', '-H']) == 0
    out = capsys.readouterr().out
    assert 'Name' not in out
    rows = [line.split('\t') for line in out.splitlines()]
    assert rows[0][0] == 'one' and rows[1][0] == 'two'


def test_list_inactive_be_shows_placeholder(fake_list_env, capsys):
    assert cli.main(['list']) == 0
    lines = [line.split() for line in capsys.readouterr().out.splitlines()]
    assert lines[3][:2] == ['three', '-']


def test_list_created_column_format(fake_list_env, capsys):
    cli.main(['list', '-H'])
    out = capsys.readouterr().out
    created = out.splitlines()[0].split('\t')[2]
    expected = time.strftime('%Y-%m-%d %H:%M',
                             time.localtime(1_750_000_000))
    assert created == expected


class TestResolvePoolReal:
    # the real body, not the monkeypatched stand-in the verb tests use

    def _with_pools(self, monkeypatch, existing):
        monkeypatch.setattr(
            engine, 'dataset_exists',
            lambda lzh, name: name in existing,
        )

    def test_explicit_pool_wins(self, monkeypatch):
        self._with_pools(monkeypatch, {'tank/ROOT'})
        assert cli._resolve_pool('LZH', 'tank') == 'tank'

    def test_boot_pool_preferred_when_both_exist(self, monkeypatch):
        self._with_pools(monkeypatch, {'boot-pool/ROOT', 'freenas-boot/ROOT'})
        assert cli._resolve_pool('LZH', None) == 'boot-pool'

    def test_freenas_boot_fallback(self, monkeypatch):
        self._with_pools(monkeypatch, {'freenas-boot/ROOT'})
        assert cli._resolve_pool('LZH', None) == 'freenas-boot'

    def test_no_root_anywhere_raises(self, monkeypatch):
        self._with_pools(monkeypatch, set())
        with pytest.raises(BEError, match='no boot environment root'):
            cli._resolve_pool('LZH', None)

    def test_explicit_pool_without_root_raises(self, monkeypatch):
        self._with_pools(monkeypatch, {'boot-pool/ROOT'})
        with pytest.raises(BEError, match='tank'):
            cli._resolve_pool('LZH', 'tank')




def test_invalid_pool_flag_is_typed_error(monkeypatch, capsys):
    monkeypatch.setattr(cli, '_open_handle', lambda: 'LZH')
    assert cli.main(['--pool', 'bad@pool', 'list']) == 1
    assert 'is not a valid pool name' in capsys.readouterr().err
