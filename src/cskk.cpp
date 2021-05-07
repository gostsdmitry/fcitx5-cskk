/*
 * Copyright (c) 2021 Naoaki Iwakiri
 * This program is released under GNU General Public License version 3 or later
 * You should have received a copy of the GNU General Public License along with
 * this program. If not, see <https://www.gnu.org/licenses/>.
 *
 * Creation Date: 2021-04-30
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileType: SOURCE
 * SPDX-FileName: cskk.cpp
 * SPDX-FileCopyrightText: Copyright (c) 2021 Naoaki Iwakiri
 */
#include "cskk.h"
#include "cskkcandidatelist.h"
#include "log.h"
#include <cstdlib>
#include <cstring>
#include <fcitx-config/iniparser.h>
#include <fcitx/addonmanager.h>
#include <fcitx/inputpanel.h>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>
using std::getenv;
using std::string;

FCITX_DEFINE_LOG_CATEGORY(cskk_log, "cskk");

namespace fcitx {

/*******************************************************************************
 * FcitxCskkEngine
 ******************************************************************************/
const string FcitxCskkEngine::config_file_path = string{"conf/fcitx5-cskk"};

const uint FcitxCskkEngine::pageStartIdx = 3;
const CandidateLayoutHint FcitxCskkEngine::layoutHint =
    CandidateLayoutHint::Horizontal;

FcitxCskkEngine::FcitxCskkEngine(Instance *instance)
    : instance_{instance}, factory_([this](InputContext &ic) {
        auto newCskkContext = new FcitxCskkContext(this, &ic);
        newCskkContext->applyConfig();
        return newCskkContext;
      }) {
  reloadConfig();
  instance_->inputContextManager().registerProperty("cskkcontext", &factory_);
}
FcitxCskkEngine::~FcitxCskkEngine() = default;
void FcitxCskkEngine::keyEvent(const InputMethodEntry &, KeyEvent &keyEvent) {
  CSKK_DEBUG() << "Engine keyEvent start: " << keyEvent.rawKey();
  // delegate to context
  auto ic = keyEvent.inputContext();
  auto context = ic->propertyFor(&factory_);
  context->keyEvent(keyEvent);
  CSKK_DEBUG() << "Engine keyEvent end";
}
void FcitxCskkEngine::save() {}
void FcitxCskkEngine::activate(const InputMethodEntry &, InputContextEvent &) {}
void FcitxCskkEngine::deactivate(const InputMethodEntry &entry,
                                 InputContextEvent &event) {
  FCITX_UNUSED(entry);
  reset(entry, event);
}
void FcitxCskkEngine::reset(const InputMethodEntry &entry,
                            InputContextEvent &event) {
  FCITX_UNUSED(entry);
  CSKK_DEBUG() << "Reset";
  auto ic = event.inputContext();
  auto context = ic->propertyFor(&factory_);
  context->reset();
}
void FcitxCskkEngine::setConfig(const RawConfig &config) {
  CSKK_DEBUG() << "Cskk setconfig";
  config_.load(config, true);
  safeSaveAsIni(config_, FcitxCskkEngine::config_file_path);
  reloadConfig();
}
void FcitxCskkEngine::reloadConfig() {
  CSKK_DEBUG() << "Cskkengine reload config";
  readAsIni(config_, FcitxCskkEngine::config_file_path);

  loadDictionary();
  if (factory_.registered()) {
    instance_->inputContextManager().foreach ([this](InputContext *ic) {
      auto context = ic->propertyFor(&factory_);
      context->applyConfig();
      return true;
    });
  }
}
void FcitxCskkEngine::loadDictionary() {
  freeDictionaries();

  const std::filesystem::directory_options directoryOptions =
      (std::filesystem::directory_options::skip_permission_denied |
       std::filesystem::directory_options::follow_directory_symlink);

  // For time being, just load files from XDG_DATA_{HOME,DIRS} only.
  try {
    std::filesystem::path dataDir = getXDGDataHome();
    dataDir.append("fcitx5-cskk/dictionary");
    CSKK_DEBUG() << dataDir;
    for (const auto &file :
         std::filesystem::directory_iterator(dataDir, directoryOptions)) {
      if (file.is_regular_file()) {
        dictionaries_.emplace_back(
            skk_user_dict_new(file.path().c_str(), "UTF-8"));
      }
    }
  } catch (std::filesystem::filesystem_error &ignored) {
    CSKK_WARN() << "Read userdictionary failed. Skipping.";
  }

  std::vector<string> xdgDataDirs = getXDGDataDirs();

  for (const auto &xdgDataDir : xdgDataDirs) {
    std::filesystem::path dataDir = xdgDataDir;
    dataDir.append("fcitx5-cskk/dictionary");
    CSKK_DEBUG() << dataDir;
    try {
      for (const auto &file :
           std::filesystem::directory_iterator(dataDir, directoryOptions)) {
        if (file.is_regular_file()) {
          dictionaries_.emplace_back(
              skk_file_dict_new(file.path().c_str(), "UTF-8"));
        }
      }
    } catch (std::filesystem::filesystem_error &ignored) {
      CSKK_WARN() << "Read static dictionary failed. Skipping.";
    }
  }
}
std::string FcitxCskkEngine::getXDGDataHome() {
  const char *xdgDataHomeEnv = getenv("XDG_DATA_HOME");
  const char *homeEnv = getenv("$HOME");
  if (xdgDataHomeEnv) {
    return string(xdgDataHomeEnv);
  } else if (homeEnv) {
    return string(homeEnv) + "/.local/share";
  }
  return "";
}

std::vector<std::string> FcitxCskkEngine::getXDGDataDirs() {
  const char *xdgDataDirEnv = getenv("XDG_DATA_DIRS");
  string rawDirs;

  if (xdgDataDirEnv) {
    rawDirs = string(xdgDataDirEnv);
  } else {
    rawDirs = "/usr/local/share:/usr/share";
  }

  std::vector<string> xdgDataDirs;
  auto offset = 0;
  while (true) {
    auto pos = rawDirs.find(':', offset);

    if (pos == std::string::npos) {
      xdgDataDirs.emplace_back(rawDirs.substr(offset));
      break;
    }
    xdgDataDirs.emplace_back(rawDirs.substr(offset, pos - offset));
    offset = pos + 1;
  }
  CSKK_DEBUG() << xdgDataDirs;
  return xdgDataDirs;
}
void FcitxCskkEngine::freeDictionaries() {
  CSKK_DEBUG() << "Cskk free dict";
  for (auto dictionary : dictionaries_) {
    skk_free_dictionary(dictionary);
  }
  dictionaries_.clear();
}

/*******************************************************************************
 * CskkContext
 ******************************************************************************/

FcitxCskkContext::FcitxCskkContext(FcitxCskkEngine *engine, InputContext *ic)
    : context_(skk_context_new(nullptr, 0)), ic_(ic), engine_(engine) {
  CSKK_DEBUG() << "Cskk context new";
  skk_context_set_input_mode(context_, *engine_->config().inputMode);
}
FcitxCskkContext::~FcitxCskkContext() = default;
void FcitxCskkContext::keyEvent(KeyEvent &keyEvent) {
  auto candidateList = std::dynamic_pointer_cast<FcitxCskkCandidateList>(
      ic_->inputPanel().candidateList());
  if (candidateList != nullptr && !candidateList->empty()) {
    if (handleCandidateSelection(candidateList, keyEvent)) {
      updateUI();
      return;
    }
  }

  if (skk_context_get_composition_mode(context_) !=
      CompositionMode::CompositionSelection) {
    CSKK_DEBUG() << "not composition selection. destroy candidate list.";
    ic_->inputPanel().setCandidateList(nullptr);
  }

  uint32_t modifiers =
      static_cast<uint32_t>(keyEvent.rawKey().states() & KeyState::SimpleMask);
  CskkKeyEvent *cskkKeyEvent = skk_key_event_new_from_fcitx_keyevent(
      keyEvent.rawKey().sym(), modifiers, keyEvent.isRelease());

  if (skk_context_process_key_event(context_, cskkKeyEvent)) {
    CSKK_DEBUG() << "Key processed in context.";
    keyEvent.filterAndAccept();
  }

  if (keyEvent.filtered()) {
    updateUI();
  }
}
bool FcitxCskkContext::handleCandidateSelection(
    const std::shared_ptr<FcitxCskkCandidateList> &candidateList,
    KeyEvent &keyEvent) {
  if (keyEvent.isRelease()) {
    return false;
  }
  CSKK_DEBUG() << "handleCandidateSelection";

  if (keyEvent.key().checkKeyList(std::vector{Key(FcitxKey_Up)})) {
    candidateList->prevCandidate();
    keyEvent.filterAndAccept();
  } else if (keyEvent.key().checkKeyList(
                 std::vector{Key(FcitxKey_Down), Key(FcitxKey_space)})) {
    CSKK_DEBUG() << "space key caught in handle candidate";

    candidateList->nextCandidate();
    keyEvent.filterAndAccept();
  } else if (keyEvent.key().check(Key(FcitxKey_Page_Down))) {
    keyEvent.filterAndAccept();
  } else if (keyEvent.key().check(Key(FcitxKey_Page_Up))) {
    keyEvent.filterAndAccept();
  } else if (keyEvent.key().check(Key(FcitxKey_Return))) {
    CSKK_DEBUG() << "return key caught in handle candidate";
    candidateList->candidate(candidateList->cursorIndex()).select(ic_);
    keyEvent.filterAndAccept();
  }

  return keyEvent.filtered();
}

void FcitxCskkContext::reset() {
  skk_context_reset(context_);
  updateUI();
}
void FcitxCskkContext::updateUI() {
  auto &inputPanel = ic_->inputPanel();

  // Output
  if (auto output = skk_context_poll_output(context_)) {
    CSKK_DEBUG() << "output: " << output;
    if (strlen(output) > 0) {
      ic_->commitString(output);
    }
    skk_free_string(output);
  }

  // Preedit
  auto preedit = skk_context_get_preedit(context_);
  Text preeditText;
  // FIXME: Pretty text format someday.
  preeditText.append(std::string(preedit));
  preeditText.setCursor(static_cast<int>(strlen(preedit)));

  // CandidateList
  int current_idx = skk_context_get_current_canddiate_selection_at(context_);
  if (current_idx >= static_cast<int>(FcitxCskkEngine::pageStartIdx)) {
    char *current_to_composite = skk_context_get_current_to_composite(context_);
    auto candidateList = std::dynamic_pointer_cast<FcitxCskkCandidateList>(
        ic_->inputPanel().candidateList());
    if (candidateList == nullptr || candidateList->empty() ||
        (strcmp(candidateList->to_composite().c_str(), current_to_composite) !=
         0)) {
      // update whole candidateList only if needed
      CSKK_DEBUG() << "Set new candidate list on UI update";

      inputPanel.setCandidateList(
          std::make_unique<FcitxCskkCandidateList>(engine_, ic_));
    } else {
      // Sync in case idx has changed out of input panel
      candidateList->setCursorIndex(
          static_cast<int>(current_idx - FcitxCskkEngine::pageStartIdx));
    }

  } else {
    inputPanel.setCandidateList(nullptr);
  }

  if (ic_->capabilityFlags().test(CapabilityFlag::Preedit)) {
    inputPanel.setClientPreedit(preeditText);
    ic_->updatePreedit();
  } else {
    inputPanel.setPreedit(preeditText);
  }
  ic_->updateUserInterface(UserInterfaceComponent::InputPanel);
}
void FcitxCskkContext::applyConfig() {
  CSKK_DEBUG() << "apply config";
  skk_context_set_dictionaries(context_, engine_->dictionaries().data(),
                               engine_->dictionaries().size());
}
void FcitxCskkContext::copyTo(InputContextProperty *context) {
  auto otherContext = dynamic_cast<FcitxCskkContext *>(context);
  skk_context_set_input_mode(otherContext->context(),
                             skk_context_get_input_mode(context_));
}

/*******************************************************************************
 * FcitxCskkFactory
 ******************************************************************************/

AddonInstance *FcitxCskkFactory::create(AddonManager *manager) {
  {
    CSKK_DEBUG() << "**** CSKK FcitxCskkFactory Create ****";
    registerDomain("fcitx5-cskk", FCITX_INSTALL_LOCALEDIR);
    auto engine = new FcitxCskkEngine(manager->instance());
    return engine;
  }
}
} // namespace fcitx

FCITX_ADDON_FACTORY(fcitx::FcitxCskkFactory);
