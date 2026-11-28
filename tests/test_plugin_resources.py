# SPDX-License-Identifier: MPL-2.0
import re
import struct
import subprocess
import tempfile
import unittest
import xml.etree.ElementTree as ET
import zipfile
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent


class PluginResourcesTest(unittest.TestCase):
    def test_resources_match_manifest_and_menu(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            subprocess.run(["python3", str(ROOT / "plugin/make_resources.py"), directory], check=True)
            manifest = ET.parse(ROOT / "plugin/NbtManager.manifest").getroot()
            self.assertEqual(manifest.findtext("name"), "NbtManager")
            params = manifest.find("config/parameters")
            self.assertEqual(params.findtext("DescFile"), "{pluginDir}/NbtManager.xml")
            # Each plugin has its own connector id: 17 ActiveSeating, 200 SecurityMenu
            # (firmware), 210 NBT-AAD. Sharing 210 broke Android Auto (dev20, dev22).
            connector = manifest.find("config/dependencies/pluginAppConnector")
            self.assertEqual(connector.findtext("type"), "Etch")
            self.assertEqual(connector.findtext("id"), "220")
            menu = ET.parse(ROOT / "plugin/NbtManager.xml").getroot().find("pluginApp")
            action = menu.find("entryButton").get("action")
            self.assertIsNotNone(menu.find("actions/raAction[@id='%s']" % action))
            text_ids = {e.get("textId") for e in menu.findall("models/textIdModel")}
            image_ids = {e.get("imageId") for e in menu.findall("models/imageIdModel")}
            with zipfile.ZipFile(output / Path(params.findtext("TextDBFile")).name) as archive:
                for name in archive.namelist():
                    entries = dict(line.split("=", 1) for line in archive.read(name).decode().splitlines())
                    self.assertTrue(text_ids <= entries.keys())
            with zipfile.ZipFile(output / Path(params.findtext("ImageDBFile")).name) as archive:
                self.assertEqual(set(archive.namelist()), {i + ".png" for i in image_ids})
                for name, size in (("55001.png", (63, 54)), ("55002.png", (549, 484))):
                    image = archive.read(name)
                    self.assertEqual(image[:8], b"\x89PNG\r\n\x1a\n")
                    self.assertEqual(struct.unpack(">II", image[16:24]), size)

    def test_focus_hold_states_and_events_match_the_plugin(self):
        menu = ET.parse(ROOT / "plugin/NbtManager.xml").getroot().find("pluginApp")
        source = (ROOT / "plugin/plugin.cpp").read_text() + (ROOT / "plugin/focus_events.hpp").read_text()

        def constant(name):
            return re.search(r"const unsigned %s = (\d+);" % name, source).group(1)

        states = {e.get("id"): e for e in menu.findall("hmiStates/hmiState")}
        blank, parent = constant("kBlankState"), constant("kParentState")
        self.assertEqual(states[blank].get("parentId"), parent)
        self.assertIsNone(states[parent].get("parentId"))
        # Nothing on the hold states can be selected or act.
        for state in (blank, parent):
            components = list(states[state].find("components"))
            self.assertTrue(components)
            for component in components:
                self.assertEqual(component.tag, "image")
                self.assertIsNone(component.get("action"))
                self.assertEqual(component.find("properties/property[@id='2']").get("value"), "false")
        events = {e.get("id"): e for e in menu.findall("events/focusEvent")}
        self.assertEqual(events[constant("kHoldEvent")].get("target"), blank)
        # iDrive takes the blank state's event only from its parent, so the
        # entry first opens the parent (top-level, as NBT-AAD's menu).
        self.assertEqual(events[constant("kOpenEvent")].get("target"), parent)
        release = events[constant("kReleaseEvent")].get("targetModel")
        model = menu.find("models/raIntModel[@id='%s']" % release)
        self.assertEqual(model.get("modelType"), "NativeStateID")
        self.assertEqual(model.get("value"), menu.get("insertStateID"))
        # The preview state stays first: plugin states are numbered in XML order.
        self.assertEqual(menu.find("hmiStates/hmiState").get("id"),
                         menu.find("models/raPreviewModel").get("previewState"))

    def test_shortcut_actions_are_declared_in_the_first_entry(self):
        # iDrive delivered no App shortcut press while each action was declared
        # only in its own pluginApp (dev126); NBT-AAD's working favourite entry
        # uses an action its first pluginApp also declares.
        apps = ET.parse(ROOT / "plugin/NbtManager.xml").getroot().findall("pluginApp")
        first = re.search(r"kFirstAction = (\d+);", (ROOT / "plugin/shortcut.cpp").read_text()).group(1)
        self.assertEqual(len(apps), 4)
        for n, app in enumerate(apps[1:]):
            action = app.find("entryButton").get("action")
            self.assertEqual(int(action), int(first) + n)
            self.assertIsNotNone(app.find("actions/raAction[@id='%s']" % action))
            self.assertIsNotNone(apps[0].find("actions/raAction[@id='%s']" % action))
            self.assertIsNotNone(app.find("entryButton/properties/property[@id='55']"))


if __name__ == "__main__":
    unittest.main()
