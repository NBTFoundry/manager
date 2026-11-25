# SPDX-License-Identifier: MPL-2.0
"""Signing-tool policy validation must agree with the native policy contract."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import bridge


class MostPolicyToolTests(unittest.TestCase):
    def test_subscriptions_need_concrete_covered_functions(self):
        valid = "sub:05.00.621,rx:*.*.*"
        self.assertEqual(bridge.most_policy(valid), valid)
        for bad in ("sub:05.00.621", "rx:05.01.*,sub:05.00.621", "rx:*.*.*,sub:05.*.621",
                    "rx:*.*.*,sub:05.00.*", "rx:*.*.*,sub:05.00.001", "rx:*.*.*,sub:01.00.100",
                    "rx:*.*.*,sub:05.00.621,sub:05.00.621", "tx:*.00.*", "tx:*.*.621"):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                bridge.most_policy(bad)
        self.assertEqual(bridge.most_policy("tx:82.00.*"), "tx:82.00.*")
        # Bridge 1.9: any command, and a reserved function a rule names.
        for sends in ("tx:*.*.*", "tx:05.*.002", "tx:05.00.000"):
            with self.subTest(sends=sends):
                bridge.requirement("1.9", "most", "", sends)
                with self.assertRaisesRegex(ValueError, "bridge 1.9"):
                    bridge.requirement("1.8", "most", "", sends)

    def test_subscription_limit_and_minor(self):
        policy = "rx:*.*.*" + "".join(",sub:05.00.%03X" % (3 + i) for i in range(32))
        bridge.most_policy(policy)
        bridge.requirement("1.2", "most", "", policy)
        with self.assertRaises(ValueError):
            bridge.most_policy(policy + ",sub:05.00.FFF")
        with self.assertRaisesRegex(ValueError, "bridge 1.2"):
            bridge.requirement("1.1", "most", "", policy)


if __name__ == "__main__":
    unittest.main()
