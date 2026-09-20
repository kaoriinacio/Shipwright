#include "SohMenu.h"
#include <soh/Notification/Notification.h>
#include "SohGui.hpp"
#include "soh/OTRGlobals.h"

namespace SohGui {

using namespace UIWidgets;

void SohMenu::AddMenuModBrowser() {
    // Add Mod Browser Menu
    AddMenuEntry("Mod Browser", CVAR_SETTING("Menu.ModBrowserSidebarSection"));
    WidgetPath path;

    // Sidebar "Browse"
    path = { "Mod Browser", "Browse", SECTION_COLUMN_1 };
    AddSidebarEntry("Mod Browser", path.sidebarName, 1);

    AddWidget(path, "Mod Browser (WIP)", WIDGET_SEPARATOR_TEXT);
    AddWidget(path,
              "Esta e a primeira fase. A interface foi criada, mas ainda "
              "nao baixa mods.\n"
              "Fase 3: integrar com a API do GameBanana para listar e "
              "baixar mods direto no jogo.",
              WIDGET_TEXT);

    AddWidget(path, "Acoes", WIDGET_SEPARATOR_TEXT);
    AddWidget(path, "Atualizar lista de mods", WIDGET_BUTTON)
        .Callback([](WidgetInfo& info) {
            SPDLOG_INFO("[ModBrowser] Botao Atualizar clicado (WIP).");
            Notification::Emit({
                .message = "Mod Browser: ainda nao implementado (Fase 3).",
            });
        });

    AddWidget(path, "Abrir pasta de mods", WIDGET_BUTTON)
        .Callback([](WidgetInfo& info) {
            SPDLOG_INFO("[ModBrowser] Botao Abrir pasta clicado (WIP).");
            Notification::Emit({
                .message = "Abrir pasta de mods: ainda nao implementado.",
            });
        });

    // Sidebar "Sobre"
    path.sidebarName = "Sobre";
    AddSidebarEntry("Mod Browser", path.sidebarName, 1);
    path.column = SECTION_COLUMN_1;

    AddWidget(path, "Informacoes", WIDGET_SEPARATOR_TEXT);
    AddWidget(path,
              "Inspirado no Mod Browser do Dusklight v2.0.0.\n"
              "Fonte de mods planejada: GameBanana API.\n"
              "Formato de mod do SoH: .otr / .o2r.",
              WIDGET_TEXT);
}

} // namespace SohGui