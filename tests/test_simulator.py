from __future__ import annotations

import unittest

from ifc_cambricon_llm.profiles import load_profiles
from ifc_cambricon_llm.simulator import derive_tile_model, simulate_reproduction


class SimulatorTest(unittest.TestCase):
    def test_cambricon_s_tile_shape_matches_paper_study(self) -> None:
        profiles = load_profiles()
        tile = derive_tile_model(profiles["platforms"]["cam_llm_s"])
        self.assertAlmostEqual(tile.tile_height, 256.0, places=6)
        self.assertAlmostEqual(tile.tile_width, 2048.0, places=6)
        self.assertGreater(tile.alpha_read_compute, 0.35)
        self.assertLess(tile.alpha_read_compute, 0.36)

    def test_reproduction_error_stays_within_basic_boundary(self) -> None:
        rows, summary = simulate_reproduction(load_profiles())
        self.assertEqual(len(rows), 21)
        self.assertLessEqual(summary.max_abs_relative_error_pct, 15.0)
        self.assertLessEqual(summary.mean_abs_relative_error_pct, 9.0)

    def test_ablation_outputs_are_present(self) -> None:
        rows, _ = simulate_reproduction(load_profiles())
        for row in rows:
            self.assertGreater(row.speedup_vs_no_read_slicing, 1.55)
            self.assertLess(row.speedup_vs_no_read_slicing, 1.75)
            self.assertGreater(row.speedup_vs_no_tiling, 1.25)
            self.assertLess(row.speedup_vs_no_tiling, 1.36)


if __name__ == "__main__":
    unittest.main()

