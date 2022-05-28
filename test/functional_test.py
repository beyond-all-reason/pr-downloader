#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2022 Marek Rusinowski
# SPDX-License-Identifier: GPL-2.0-or-later OR Apache-2.0 OR MIT
#
# mypy: warn-unused-configs, disallow-any-generics, disallow-subclassing-any,
# mypy: disallow-untyped-calls, disallow-untyped-defs, disallow-incomplete-defs,
# mypy: check-untyped-defs, disallow-untyped-decorators, no-implicit-optional,
# mypy: warn-redundant-casts, warn-unused-ignores, warn-return-any,
# mypy: no-implicit-reexport, strict-equality, disallow-any-expr
#
# File formatted with `python3 -m yapf --style=google`.
#
"""Functional tests for pr-downloader.

The tests build a rapid repo in temporary directory, host it from HTTP server
on localhost and run the pr-downloader to verify the behavior.
"""

from __future__ import annotations
from contextlib import contextmanager, AbstractContextManager
from dataclasses import dataclass, field
from http import HTTPStatus
from typing import cast, Callable, ClassVar, Generator, Iterable, Optional
import argparse
import binascii
import gzip
import hashlib
import http.server
import os
import os.path
import shutil
import struct
import subprocess
import sys
import tempfile
import threading
import time
import unittest


class RapidFile:
    """Base class for all files stores under Rapid root directory."""
    __final_hash: Optional[bytes]  # valid only after call to get_contents

    def serialize(self) -> bytes:
        """Produces the uncompressed contents of the file."""
        ...

    def rapid_filename(self) -> str:
        ...

    # -> (subdirectory to use for children, child rapid files)
    def children_files(self) -> tuple[str, Iterable[RapidFile]]:
        ...

    @property
    def final_hash(self) -> bytes:
        if self.__final_hash is None:
            self.get_contents()
        assert self.__final_hash is not None
        return self.__final_hash

    def get_contents(self) -> bytes:
        """Generates final compressed file and caches hash."""
        contents = gzip.compress(self.serialize(), compresslevel=3, mtime=0)
        self.__final_hash = hashlib.blake2b(contents).digest()
        return contents

    def traverse(self) -> Iterable[tuple[str, RapidFile]]:
        """Traverses through full tree of RapidFile children."""
        yield '', self
        sub, children = self.children_files()
        for child in children:
            for d, rf in child.traverse():
                yield os.path.join(sub, d), rf

    def is_identical(self, file_path: str) -> bool:
        with open(file_path, 'rb') as f:
            return hashlib.blake2b(f.read()).digest() == self.final_hash


@dataclass
class ArchiveFile(RapidFile):
    """Represents File referenced by in the Sdp archive stored in Rapid pool."""
    filename: str
    md5: bytes
    crc32: int
    size: int
    contents: bytes

    def serialize(self) -> bytes:
        return self.contents

    def rapid_filename(self) -> str:
        b = self.md5.hex()
        return os.path.join('pool', b[0:2], f'{b[2:]}.gz')

    def children_files(self) -> tuple[str, Iterable[RapidFile]]:
        return '', []


@dataclass
class Archive(RapidFile):
    """Represents Sdp index file stored in the repository."""
    shortname: str
    depends: str
    name: str
    # filename -> ArchiveFile
    files: dict[str, ArchiveFile] = field(default_factory=dict)

    def serialize(self) -> bytes:
        out = []
        for n, f in sorted(list(self.files.items())):
            assert n == f.filename
            filename_bytes = f.filename.encode()
            out.append(struct.pack('B', len(filename_bytes)))
            out.append(filename_bytes)
            out.append(f.md5)
            out.append(struct.pack('>II', f.crc32, f.size))
        return b''.join(out)

    def get_md5(self) -> str:
        digest = hashlib.md5()
        for _, f in sorted(list(self.files.items())):
            digest.update(hashlib.md5(f.filename.encode()).digest())
            digest.update(f.md5)
        return digest.hexdigest()

    def rapid_filename(self) -> str:
        return os.path.join('packages', f'{self.get_md5()}.sdp')

    def children_files(self) -> tuple[str, Iterable[RapidFile]]:
        return '', self.files.values()

    def add_file(self, filename: str, contents: bytes) -> ArchiveFile:
        assert len(filename) > 0 and filename[0] != '/'
        assert filename not in self.files
        assert len(contents) > 0
        file = ArchiveFile(filename=filename,
                           md5=hashlib.md5(contents).digest(),
                           crc32=binascii.crc32(contents),
                           size=len(contents),
                           contents=contents)
        self.files[filename] = file
        return file


@dataclass
class Repo(RapidFile):
    """Represents Rapid repository containing set of archives."""
    shortname: str
    base_url: str
    # shortname -> Archive
    archives: dict[str, Archive] = field(default_factory=dict)

    def serialize(self) -> bytes:
        out = []
        for s, a in sorted(list(self.archives.items())):
            assert s == a.shortname
            out.append(f'{a.shortname},{a.get_md5()},{a.depends},{a.name}\n')
        return ''.join(out).encode()

    def rapid_filename(self) -> str:
        return os.path.join(self.shortname, 'versions.gz')

    def children_files(self) -> tuple[str, Iterable[RapidFile]]:
        return self.shortname, self.archives.values()

    def add_archive(self,
                    tag: str,
                    name: Optional[str] = None,
                    depends: str = '') -> Archive:
        assert len(tag) > 0
        shortname = f'{self.shortname}:{tag}'
        assert shortname not in self.archives
        archive = Archive(shortname=shortname,
                          depends=depends,
                          name=shortname if name is None else name)
        self.archives[shortname] = archive
        return archive


class RapidRoot(RapidFile):
    """Base main index storing references to all available Rapid repos.

    This is the only object that should be created directly, all other should
    be created using respective add_* functions.
    """
    base_url: str
    # shortname -> Repo
    repos: dict[str, Repo]

    def __init__(self, base_url: str):
        assert base_url[-1] != '/'
        self.base_url = base_url
        self.repos = dict()

    def add_repo(self, shortname: str) -> Repo:
        assert len(shortname) > 0 and shortname not in self.repos
        repo = Repo(shortname=shortname,
                    base_url=f'{self.base_url}/{shortname}')
        self.repos[shortname] = repo
        return repo

    def serialize(self) -> bytes:
        out = []
        for shortname, repo in sorted(list(self.repos.items())):
            assert shortname == repo.shortname
            out.append(f'{repo.shortname},{repo.base_url},,\n')
        return ''.join(out).encode()

    def rapid_filename(self) -> str:
        return 'repos.gz'

    def children_files(self) -> tuple[str, Iterable[RapidFile]]:
        return '', self.repos.values()

    def save(self, directory: str) -> None:
        """Saves the created in-memory rapid repo to `directory`."""
        for sub, f in self.traverse():
            path = os.path.join(directory, sub, f.rapid_filename())
            if os.path.dirname(path):
                os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, 'wb') as out:
                out.write(f.get_contents())


class TestingHTTPServer(http.server.ThreadingHTTPServer):
    directory: str
    rapid: Optional[RapidRoot]
    _resolvers: list[Callable[[str], HTTPStatus]]
    _resolver_locks: list[threading.Lock]

    def __init__(self, directory: str) -> None:
        self.directory = directory
        self._resolvers = []
        self._resolver_locks = []
        self.rapid = None
        super().__init__(('localhost', 0), self.create_handler)

    def create_handler(
            self, request: bytes, client_address: tuple[str, int],
            server: TestingHTTPServer) -> http.server.BaseHTTPRequestHandler:

        class Handler(http.server.SimpleHTTPRequestHandler):

            def do_GET(self) -> None:
                for l, r in zip(server._resolver_locks, server._resolvers):
                    # Call resolver under lock because each request is handled
                    # in a separate thread so we need to make sure that there
                    # aren't any data races if resolver modifies some state.
                    with l:
                        status = r(self.path)
                    if status != HTTPStatus.OK:
                        self.send_error(status)
                        return
                super().do_GET()

            def do_POST(self) -> None:
                # All POSTSs from pr-downloader are only for the streamer, and
                # this function does a very basic simulation of execution of
                # the real streamer cgi program.

                path, query = self.path.rsplit('?', 1)
                if not path.endswith('streamer.cgi'):
                    self.send_error(HTTPStatus.NOT_FOUND, 'no streamer')
                    return
                if server.rapid is None:
                    self.send_error(HTTPStatus.INTERNAL_SERVER_ERROR)
                    return
                shortname = path.split('/')[1]
                if shortname not in server.rapid.repos:
                    self.send_error(HTTPStatus.NOT_FOUND, 'no repo')
                    return
                for a in server.rapid.repos[shortname].archives.values():
                    if a.get_md5() == query:
                        archive = a
                        break
                else:
                    self.send_error(HTTPStatus.NOT_FOUND, 'no archive')
                    return

                length = int(cast(str, self.headers.get('content-length')))
                data = self.rfile.read(length)
                files_to_get = []
                for b in gzip.decompress(data):
                    files_to_get.extend([bool(b & (1 << i)) for i in range(8)])

                out = []
                files_sorted = [
                    f for _, f in sorted(list(archive.files.items()))
                ]
                for get, f in zip(files_to_get, files_sorted):
                    if not get:
                        continue
                    data = f.get_contents()
                    out.append(struct.pack('>I', len(data)))
                    out.append(data)

                result = b''.join(out)

                self.send_response(HTTPStatus.OK, "Ok")
                self.send_header('Content-Type', 'application/octet-stream')
                self.send_header('Content-Length', str(len(result)))
                self.end_headers()
                self.wfile.write(result)

        return Handler(request,
                       client_address,
                       server,
                       directory=self.directory)

    def add_resolver(self, resolver: Callable[[str], HTTPStatus]) -> None:
        self._resolvers.append(resolver)
        self._resolver_locks.append(threading.Lock())

    def clear_resolvers(self) -> None:
        self._resolvers = []
        self._resolver_locks = []

    @contextmanager
    def serve(self) -> Generator[None, None, None]:
        t = threading.Thread(target=self.serve_forever)
        t.start()
        try:
            yield
        finally:
            self.shutdown()
            t.join()


# Related closed issue upstream: https://bugs.python.org/issue25024
def create_temp_dir(suffix: Optional[str] = None,
                    prefix: Optional[str] = None,
                    dir: Optional[str] = None,
                    delete: bool = True) -> AbstractContextManager[str]:
    """Wrapper to support not-deleting temporary directory."""
    if delete:
        return tempfile.TemporaryDirectory(suffix=suffix,
                                           prefix=prefix,
                                           dir=dir)

    @contextmanager
    def h() -> Generator[str, None, None]:
        yield tempfile.mkdtemp(suffix=suffix, prefix=prefix, dir=dir)

    return h()


def fail_requests_resolver(
        paths: dict[str, HTTPStatus]) -> Callable[[str], HTTPStatus]:

    def resolver(path: str) -> HTTPStatus:
        if path in paths:
            return paths[path]
        return HTTPStatus.OK

    return resolver


class TestDownloading(unittest.TestCase):
    pr_downloader_path: ClassVar[str]
    keep_temp_files: ClassVar[bool]
    rapid: RapidRoot
    serving_root: str
    dest_root: str
    server: TestingHTTPServer

    def call_rapid_download(self,
                            shortname: str,
                            use_streamer: bool = False) -> int:
        with tempfile.NamedTemporaryFile(
                prefix='pr-run-', delete=not self.keep_temp_files) as out:
            if self.keep_temp_files:
                print(f'run output: {out.name}')
            res = subprocess.run(
                [
                    self.pr_downloader_path, '--filesystem-writepath',
                    self.dest_root, '--rapid-download', shortname
                ],
                stderr=subprocess.STDOUT,
                stdout=out,
                timeout=10,
                env={
                    'PRD_RAPID_REPO_MASTER':
                        f'{self.rapid.base_url}/{self.rapid.rapid_filename()}',
                    'PRD_RAPID_USE_STREAMER':
                        'true' if use_streamer else 'false',
                })
            return res.returncode

    def verify_downloaded_rapid(self, archive: str | Archive) -> bool:
        if isinstance(archive, str):
            repo = archive.split(':', 1)[0]
            archive = self.rapid.repos[repo].archives[archive]
        # We ignore subdir because in destination it's always flat structure.
        for _, rf in archive.traverse():
            dest_file = os.path.join(self.dest_root, rf.rapid_filename())
            if not os.path.exists(dest_file) or not rf.is_identical(dest_file):
                return False
        return True

    def clear_dest_root(self) -> None:
        for f in os.listdir(self.dest_root):
            path = os.path.join(self.dest_root, f)
            if os.path.isdir(path):
                shutil.rmtree(path)
            else:
                os.remove(path)

    def run(self, result: Optional[unittest.TestResult] = None) -> None:
        with create_temp_dir(prefix='pr-serv-',
                             delete=not self.keep_temp_files) as serving_root,\
             create_temp_dir(prefix='pr-dest-',
                             delete=not self.keep_temp_files) as dest_root,\
             TestingHTTPServer(serving_root) as server:
            self.serving_root = serving_root
            self.dest_root = dest_root
            if self.keep_temp_files:
                print(f'serving: {serving_root}')
                print(f'destination: {dest_root}')
            self.server = server
            self.rapid = RapidRoot(
                f'http://{server.server_address[0]}:{server.server_address[1]}')
            self.server.rapid = self.rapid
            super().run(result)

    def _base_simple_download_ok(self, use_streamer: bool) -> None:
        repo = self.rapid.add_repo('testrepo')
        archive = repo.add_archive('pkg:1')
        archive.add_file('a.txt', b'a')
        archive.add_file('b.txt', b'aa')
        repo = self.rapid.add_repo('testrepo2')
        archive = repo.add_archive('pkg:2')
        archive.add_file('a.txt', b'a')
        archive.add_file('b.txt', b'aacc')
        archive = repo.add_archive('pkg:3')
        archive.add_file('a.txt', b'aeee')
        self.rapid.save(self.serving_root)

        with self.server.serve():
            self.assertEqual(
                self.call_rapid_download('testrepo:pkg:1',
                                         use_streamer=use_streamer), 0)
            self.assertEqual(
                self.call_rapid_download('testrepo2:pkg:2',
                                         use_streamer=use_streamer), 0)

        self.assertTrue(self.verify_downloaded_rapid('testrepo:pkg:1'))
        self.assertTrue(self.verify_downloaded_rapid('testrepo2:pkg:2'))
        self.assertFalse(self.verify_downloaded_rapid('testrepo2:pkg:3'))

    def test_simple_download_ok(self) -> None:
        self._base_simple_download_ok(use_streamer=False)

    def test_simple_download_ok_streamer(self) -> None:
        self._base_simple_download_ok(use_streamer=True)

    def test_rapid_host_broken(self) -> None:
        self.rapid.save(self.serving_root)
        self.server.add_resolver(
            fail_requests_resolver(
                {'/repos.gz': HTTPStatus.INTERNAL_SERVER_ERROR}))
        with self.server.serve():
            self.assertNotEqual(self.call_rapid_download('testrepo:pkg:1'), 0)

    def test_all_download_failures_fail(self) -> None:
        repo = self.rapid.add_repo('testrepo')
        archive = repo.add_archive('pkg:1')
        archive.add_file('a.txt', b'a')
        archive.add_file('b.txt', b'aa')
        self.rapid.save(self.serving_root)

        # Make sure that when no errors are injected it succeeds.
        with self.server.serve():
            self.assertEqual(self.call_rapid_download('testrepo:pkg:1'), 0)

        for sub, rf in self.rapid.traverse():
            self.clear_dest_root()
            path = os.path.join('/', sub, rf.rapid_filename())
            self.server.add_resolver(
                fail_requests_resolver({path: HTTPStatus.NOT_FOUND}))
            with self.server.serve():
                self.assertNotEqual(
                    self.call_rapid_download('testrepo:pkg:1'),
                    0,
                    msg=f'downloading {path} didn\'t cause failure as expected')
            self.server.clear_resolvers()

    def _base_detect_file_corruption(self, use_streamer: bool) -> None:
        repo = self.rapid.add_repo('testrepo')
        archive = repo.add_archive('pkg:1')
        file = archive.add_file('a.txt', b'aakjsdifnsdfef')
        file.contents = b'ufeshfisuefe'
        self.rapid.save(self.serving_root)

        with self.server.serve():
            self.assertNotEqual(
                self.call_rapid_download('testrepo:pkg:1',
                                         use_streamer=use_streamer), 0)
            self.assertFalse(
                os.path.exists(
                    os.path.join(self.dest_root, file.rapid_filename())))

    def test_detect_file_corruption(self) -> None:
        self._base_detect_file_corruption(use_streamer=False)

    def test_detect_file_corruption_streamer(self) -> None:
        self._base_detect_file_corruption(use_streamer=True)

    def test_sdp_corruption_fails(self) -> None:
        repo = self.rapid.add_repo('testrepo')
        archive = repo.add_archive('pkg:1')
        file = archive.add_file('a.txt', b'a')
        self.rapid.save(self.serving_root)

        with open(
                os.path.join(self.serving_root, 'testrepo',
                             archive.rapid_filename()), 'wb') as f:
            f.write(b'asdiiada')

        with self.server.serve():
            self.assertNotEqual(self.call_rapid_download('testrepo:pkg:1'), 0)

    # TODO(marekr): Fix bugs that are being reproduced by the tests below.

    # def test_streamer_not_returning_all_files_fails(self) -> None:
    #     repo = self.rapid.add_repo('testrepo')
    #     archive = repo.add_archive('pkg:1')
    #     archive.add_file('a.txt', b'a')
    #     archive.add_file('b.txt', b'aa')
    #     self.rapid.save(self.serving_root)

    #     del archive.files['b.txt']

    #     with self.server.serve():
    #         self.assertNotEqual(
    #             self.call_rapid_download('testrepo:pkg:1'), 0)

    # def test_no_partial_overrides_to_files(self) -> None:
    #     repo = self.rapid.add_repo('rep1')
    #     archive = repo.add_archive('pkg:1')
    #     archive.add_file('a.txt', b'a')
    #     repo = self.rapid.add_repo('rep2')
    #     archive = repo.add_archive('pkg:2')
    #     archive.add_file('b.txt', b'b')
    #     self.rapid.save(self.serving_root)

    #     with self.server.serve():
    #         self.assertEqual(self.call_rapid_download('rep1:pkg:1'), 0)

    #     self.rapid.repos = {}
    #     repo = self.rapid.add_repo('rep3')
    #     archive = repo.add_archive('pkg:1')
    #     archive.add_file('c.txt', b'c')
    #     self.rapid.save(self.serving_root)

    #     with self.server.serve():
    #         self.call_rapid_download('rep2:pkg:1')

    #     # This is hardcoding implementation specific path of pr-downloader
    #     # so it's not the best, but that's actual failure case <shrug>
    #     dest_repogz = os.path.join(self.dest_root, f'rapid/{self.server.server_address[0]}-{self.server.server_address[1]}/repos.gz')
    #     with open(dest_repogz, 'rb') as f:
    #         h = hashlib.blake2b(f.read()).digest()

    #     self.assertEqual(h, self.rapid.final_hash)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument('--pr-downloader-path', required=True)
    parser.add_argument('-k',
                        '--keep-temp-files',
                        action=argparse.BooleanOptionalAction,
                        default=False)
    args, remaining_argv = parser.parse_known_args()
    TestDownloading.pr_downloader_path = args.pr_downloader_path
    TestDownloading.keep_temp_files = args.keep_temp_files
    unittest.main(argv=sys.argv[:1] + remaining_argv)
