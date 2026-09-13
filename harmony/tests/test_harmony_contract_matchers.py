import unittest

from harmony_contract_matchers import run_game_forwards_locator_and_autosave


class RunGameOpenMatcherTests(unittest.TestCase):
    def test_accepts_the_real_three_argument_call(self):
        body = "private async start() {\n" \
               "    await this.play.open(context, this.locator, this.autosaveEnabled);\n" \
               "}"
        self.assertTrue(run_game_forwards_locator_and_autosave(body))

    def test_accepts_newlines_and_extra_whitespace(self):
        body = "await\n" \
               "  this.play.open(\n" \
               "    context,\n" \
               "    this.locator,\n" \
               "    this.autosaveEnabled\n" \
               "  );"
        self.assertTrue(run_game_forwards_locator_and_autosave(body))

    def test_rejects_a_renamed_locator(self):
        body = "this.play.open(context, this.pendingLocator, this.autosaveEnabled)"
        self.assertFalse(run_game_forwards_locator_and_autosave(body))

    def test_rejects_a_missing_autosave_argument(self):
        body = "this.play.open(context, this.locator)"
        self.assertFalse(run_game_forwards_locator_and_autosave(body))

    def test_rejects_a_different_receiver(self):
        body = "this.player.open(context, this.locator, this.autosaveEnabled)"
        self.assertFalse(run_game_forwards_locator_and_autosave(body))

    def test_rejects_an_extra_argument_after_autosave(self):
        body = "this.play.open(context, this.locator, this.autosaveEnabled, extra)"
        self.assertFalse(run_game_forwards_locator_and_autosave(body))


if __name__ == '__main__':
    unittest.main()
