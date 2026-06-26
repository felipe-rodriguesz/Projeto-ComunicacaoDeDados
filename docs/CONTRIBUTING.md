# Guias de Contribuição e Divisão da Equipe

Bem-vindo à área de desenvolvimento! Para evitar conflitos de código durante a implementação final das restrições do projeto e bônus, adotamos as seguintes boas práticas de GitHub.

## 🧑‍🤝‍🧑 Papéis e Responsabilidades

*   **Felipe (Gerente do Projeto):** Integração principal (branch `main`). Responsável pela lógica de Sincronismo Inicial via auto-baud (`micros()`) e pela consolidação física dos testes.
*   **Elder (Especialista NRZ-I):** Responsável por abrir branches e codificar a transição e memória de estados do modo NRZ-I no TX e RX.
*   **Kroda (Especialista Manchester):** Responsável por abrir branches e lidar com as lógicas de alteração e leitura na metade do tempo do baud rate base no TX e RX.

## 🌿 Fluxo de Trabalho (GitHub Flow)

Nós **não codamos diretamente na branch `main`**. A branch `main` só deve conter código que compila, funciona, e não trava os Arduinos.
Siga os seguintes passos para contribuir no seu módulo:

1.  **Clone o projeto localmente:**
    `git clone https://github.com/felipe-rodriguesz/Projeto-ComunicacaoDeDados.git`
2.  **Crie a sua branch de trabalho (Feature Branch):**
    Sempre crie uma branch com o seu nome ou com a tarefa (`feat/nrz-i`, `feat/manchester`, `fix/auto-baud`).
    `git checkout -b feat/seu-nome-tarefa`
3.  **Desenvolva e faça Commits Descritivos:**
    Trabalhe no seu arquivo (`tx_arduino.ino` ou `rx_arduino.ino`). Faça pequenos commits lógicos:
    `git commit -m "feat(tx): implementa mudanca de estado no NRZ-I"`
4.  **Faça o Push para a nuvem:**
    `git push origin feat/seu-nome-tarefa`
5.  **Abra um Pull Request (PR):**
    Vá na página do repositório no GitHub, clique em "Compare & pull request". No PR, **explique** o que você alterou.
6.  **Revisão do Gerente:**
    O Felipe vai ler seu código, talvez carregar fisicamente na bancada para ver se o LED e LDR responderam à sua matemática e, se aprovar, fará o `Merge` para a branch `main`.

## 🛠️ Regras de Ouro no Código

*   **Não use a classe `String`:** Não utilize `String("Hello")`. O Uno tem pouca memória. Use a formatação tradicional baseada em `char array[]`.
*   **Não coloque `analogRead` ou `Serial.print` dentro das ISR (Interrupções):** O Timer1 congela o microcontrolador. Use sempre o conceito de Flags (variáveis `volatile bool`), e trate o evento fora da interrupção (dentro do `loop()`).
*   **Comente sua matemática:** Se você fez `total_bits = (tamanho * 8) * 2` porque é Manchester, coloque um comentário explicando. O professor poderá ler nosso código e perguntar os motivos.

## 📝 Documentação Obrigatória
Qualquer um de nós que submeter uma funcionalidade nova deve entrar no diretório `/docs`, abrir o documento oficial do relatório e preencher um parágrafo que explique a sua lógica e como você resolveu. Nós não faremos o relatório apenas na noite final!
