import hashlib
import json
import tempfile
import unittest
from pathlib import Path

import evaluator
import server


COMPLETE = "\n".join(
    f".macro {name}\n.endm" for name in evaluator.REQUIRED_MACROS
)


class EvaluatorUnitTests(unittest.TestCase):
    def test_sha_and_contract(self):
        digest = hashlib.sha256(COMPLETE.encode()).hexdigest()
        self.assertEqual(evaluator.validate_candidate(COMPLETE, digest), digest)

    def test_rejects_external_include(self):
        with self.assertRaisesRegex(evaluator.EvaluationError, "external"):
            evaluator.validate_candidate(COMPLETE + '\n.include "secret"')

    def test_parse_fragment_report(self):
        payload = {"status": "PASS", "mismatches": 0}
        parsed = evaluator.parse_fragment_report(
            evaluator.REPORT_PREFIX + json.dumps(payload) + "\n")
        self.assertEqual(parsed, payload)

    def test_private_result_is_whitelisted(self):
        result = server.parse_result_line(
            "noise\nRESULT points=8192 price=10.4 abs_err=0.01 "
            "ns_per_sample=0.06 secret=discarded\n")
        self.assertEqual(set(result), {"points", "price", "abs_err", "ns_per_sample"})


if __name__ == "__main__":
    unittest.main()
