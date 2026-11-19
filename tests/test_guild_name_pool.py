"""Exact user-supplied data contract, independent of runtime name acceptance."""
import hashlib
import re
import unittest
from pathlib import Path


class GuildNamePoolTests(unittest.TestCase):
    def test_exact_46_candidates(self):
        source = (Path(__file__).resolve().parents[1] / 'src/Guild/GuildNamePool.h').read_text()
        pool = re.search(r'GuildNamePool\s*=\s*\{\{(.*?)\}\};', source, re.S).group(1)
        names = re.findall(r'"([^"\\]*)"', pool)
        self.assertEqual(len(names), 46)
        # SHA-256 of the original task's trimmed names joined with LF, no final LF.
        self.assertEqual(hashlib.sha256('\n'.join(names).encode()).hexdigest(),
                         'c0050314bba459ec29a3e735d53f78ce70db1bdc2688c755b74904e9f9007723')
