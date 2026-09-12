#pragma once
#include <string>

namespace DiscordIPC {

// Inicializa a conexão com o Discord (abre o named pipe + handshake).
// Chame uma vez quando o jogo iniciar. Se o Discord não estiver aberto,
// falha silenciosamente — Update() tenta reconectar depois.
void Init(const std::string& clientId);

// Manda um SET_ACTIVITY pro Discord. Se não estiver conectado, tenta reconectar.
// largeImage/largeText são opcionais — passa "" pra pular.
void Update(const std::string& details,
            const std::string& state,
            const std::string& largeImage = "",
            const std::string& largeText = "");

// Ping periódico pra manter a conexão viva. Chama a cada ~1s junto com o update.
void Tick();

// Fecha o pipe. Chame no shutdown do jogo.
void Shutdown();

} // namespace DiscordIPC