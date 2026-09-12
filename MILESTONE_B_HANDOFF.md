# Milestone B — handoff da continuidade nativa, Core 0.6.0

## Estado da entrega

O percurso funcional de B foi demonstrado: criação AP limpa, dificuldade imutável,
salvamento nativo, encerramento, reabertura em outro processo, Continue jogável e
novo checkpoint após a reabertura. O operador confirmou local correto, dificuldade
e configurações preservadas, sem NOTICE na última tentativa.

Isso não equivale a um PASS integral do observador. O run final conserva sua falha:
um falso diagnóstico de readback foi identificado e corrigido em código; uma recusa
posterior de identidade do provedor, durante o encerramento relatado, continua sem
caller comprovado. A correção final do resultado nativo passou nos testes controlados,
mas ainda não foi executada no jogo. A conclusão adequada é **aceitação funcional
de B, com encerramento da observação ainda pendente**, não uma certificação sem ressalvas.

Core continua 0.6.0. A identidade da build deriva dos arquivos-fonte, não do nome
do milestone. A build retail confirmada foi `b7ad677518aa`; a build da correção final
é distinta e deve ser identificada pelo SOURCE-IDENTITY de seu pacote imutável.
Não trocar DLLs de um candidato mantendo o manifesto de outro.

## Contrato preservado

- DOOM continua responsável pela serialização, codificação, autenticação, leitura,
  parser e restauração do mundo. Sentinel associa e verifica esses caminhos nativos.
- Namespace AP, geração, dono nativo e reserva permanecem separados de vanilla.
  Metadados de continuidade não substituem os arquivos nativos.
- Configuração da sala controla a dificuldade; New Game não abre outra escolha.
  O teste retail confirmado usou Base/Nightmare, valor 3. Outras dificuldades têm
  cobertura controlada, sem alegação de validação retail equivalente.
- PROFILE conserva settings/skins, identidade/baseline semântico, catálogo AP e o
  terceiro argumento de preparação como contexto de usuário nativo.
- Escritas precisam de operação correlacionada, conclusão SDK, readback e hashes.
  O checkpoint fica pendente antes da mutação e só é confirmado depois das provas.
- Continue exige fonte correspondente, parser nativo, dificuldade e mapa corretos.
  Leitura de metadados no menu não demonstra gameplay restaurado.
- Casos falhos e checkpoints foram preservados. Não houve recriação automática do
  save para fazer uma tentativa posterior passar, nem supressão da interface NOTICE.

## Histórico causal

| Etapa/caso | Evidência e correção |
| --- | --- |
| Base de A e início de B | Preservação do isolamento, dono e PROFILE; ligação entre New Game, opções imutáveis, transição, fábrica nativa, write/SDK/readback e continuidade entre processos. |
| Tentativas e176/1729 | NOTICE e falhas de PROFILE precederam problemas de memória. A evidência antiga não continha o primeiro predicado específico; não se atribuiu uma causa retrospectiva inventada. Foram detalhadas preparação, serialização, resultado e publicação de PROFILE. |
| 690726 | Investigação de instalação retida/não observada e parser sem fonte correlacionada. Qualificação de startup e rastreamento do processo exato permaneceram obrigatórios. |
| 14e09 / startup e1b8 | Startup perdido antes do menu: ordenação da instalação/publicação e gate de RootInit corrigidos. |
| 216ff / afafd | O serializer deixava o cursor dentro de idMasterLevelManager. O escritor nativo codificava a raiz original. Core passou a validar/restaurar seleção na raiz retida, preservando o cursor retornado e o restante da árvore. PROFILE real de 561.861 bytes passou posteriormente. |
| c9 após reboot | Timeout sem processo/capturas e sem payload de campanha. Recuperação revisada e comparação exata preservaram o caso; não se interpretou a tentativa não observada como gameplay. |
| 71c / c373 | Vetor SDK contém idFile_Memory inline de 384 bytes, criado a partir de streams idFile_SaveGame. Core e fixtures exigiam erroneamente o tipo derivado no transporte. Exigência corrigida apenas nessa fronteira; buffers, nomes, ownership, limites e hashes continuam estritos. |
| 9d | Primeiro checkpoint retail realmente salvo, lido de volta e persistido: quatro arquivos. Depois houve recusa de identidade na saída. Auditoria preservou 801 arquivos do caso e confirmou os payloads. |
| 82cc / saída nativa | Invalidação ganhou origem/caller/stack. Apenas destruição nativa de root comprovada e manager correspondente permitem retirar acesso ao provedor conservando ownership/rotas. Reset/account loss durante sessão continuam recusados. O antigo caller de 9d não foi inferido como comprovado. |
| 1a | Leitura do menu completava com sucesso, mas era enviada como conclusão de escrita com operação zero. Isso produzia native_checkpoint_completion_unproven e bloqueava o parser. Auditoria preservou 683 arquivos, 23 registros e o checkpoint anterior. |
| b7ad / leitura e parser | Apenas writes notificam conclusão de checkpoint. Parser de metadados e parser de gameplay têm associações diferentes. O leitor nativo pede o grupo principal ou backup completo, não os quatro arquivos do recibo de escrita de uma vez. |
| fb16, timeout | Nenhum processo/captura observado. Rearme conservou identidade e checkpoint. O primeiro rearme deixou caminhos de evidência existentes; isso provocou FileExistsError em proteção/exportação. |
| fb16, reparo do rearme | 603 arquivos foram arquivados integralmente; caminhos descartáveis do observador foram recriados, preservando fontes e criação original. Proteção e exportação foram executadas antes da nova tentativa. Isso foi recuperação pontual, não uma correção geral do runner para qualquer retry. |
| fb16, confirmação retail | Continue correto/jogável, dificuldade/configurações corretas, nenhum NOTICE. Metadados e parser de gameplay passaram; novo checkpoint 2 foi salvo e verificado. O observador ainda recusou um resultado booleano interpretado com largura errada. |
| Correção final | O decoder escreve um byte no sucesso e quatro bytes no erro. Core agora lê somente o membro ativo; bytes superiores de uma variante inativa não transformam sucesso em falha. Erros nativos completos, false e buffers de retorno são preservados. |

## Prova final e seus limites

O caso é `retest-fb16ea4a638e4bab8cc8ac5c1fb11b5d`. A criação original continua
proveniente de 9d; a tentativa de resume 1a permaneceu separada e não foi promovida
a criação bem-sucedida.

- Sequências 70/94: hashes do par de metadados verificados; 77/101: parser nativo
  de metadados concluído sem marcar gameplay restaurado.
- Sequências 121/128: fonte do Continue verificada e parser de gameplay concluído.
- Sequências 217/222/227/228: submissão, callback, resultado SDK e resultado de
  escrita do provedor bem-sucedidos.
- Sequência 251: primeiro diagnóstico recusado, readback_native_decode_result.
  O discriminante era sucesso e seu byte booleano era verdadeiro. Os bytes
  superiores não inicializados como parte desse membro causavam a comparação errada.
- Sequência 253: conclusão nativa de readback bem-sucedida.
- Sequência 262: checkpoint 2 persistido, source_checkpoint 1, dois arquivos,
  native_saved/readback_verified/continuity_persisted verdadeiros.
- Sequência 264: recusa provider_identity, cerca de dez segundos depois. Sua origem
  exata não está comprovada. Não foi removida, ignorada ou convertida em sucesso.

A auditoria final reteve 932 arquivos do caso, reconciliou 22 registros nativos e
o latest, verificou os dois payloads do checkpoint 2 e repetiu a comparação com os
30 arquivos vanilla, sem alterações. Seu SHA256 é
`65a1d7ba7ba40bf70b7fcba355312371b0efcdcf2a6f1c080a4b774790fb9c9c`.
As evidências privadas ficam em SentinelDocs/artifacts/milestone-b-fb16-retail-audit;
não foram incorporadas ao Git público.

## Código e verificação

Fronteiras principais: save_campaign, save_campaign_native, save_write,
save_sdk_write, save_readback, save_provider, save_session e o runner.

- ReadWorkerResult conserva layout/ABI; active_value lê um byte no sucesso e
  quatro no erro. Diagnóstico e captura de backup usam esse mesmo contrato.
- Fixtures de readback incluem sucesso com bytes superiores sujos, false com
  bytes superiores sujos e erro de 32 bits cujo byte baixo parece verdadeiro.
  Verificam também que o retorno nativo não foi reescrito.
- O fixture integrado usa sucesso com esses bytes superiores em leituras de menu,
  Continue e checkpoint; o future nativo substituído propaga o membro ativo.
- Testes usam os adapters reais de Core, com funções nativas/Steam substituídas.
  Não são uma execução do DOOM retail.
- Build MSVC completa com um worker: passou. CTest: **28/28**, 34,81 s.
- Continuidade em processos separados: **11/11**, 6,040 s. Inclui par principal e
  backup, menu → Continue → checkpoint 2, corrupção/subconjunto/grupo misto,
  parser/caller/mode, quatro dificuldades e PROFILE checkpoint 2 → reopen → 3.
- O runner manteve sua fonte idêntica ao candidato anterior; sua prova de **48
  testes** é herdada por hash, não apresentada como executada novamente agora.
- O produtor C++ de diagnósticos é verificado pelo sanitizer de produção: 24
  estágios, primeira causa preservada, histórico limitado e dados privados omitidos.
  Contagens/bytes e hashes exatos estão no recibo da build.
- Prova do disco suportado e assinaturas permanece herdada e explicitamente
  atribuída. Uma atualização do executável exige nova qualificação.

RTK foi utilizado; build e testes ocorreram sequencialmente. Serena foi desabilitado
na configuração local/global após o episódio de uso elevado de memória, mantendo
a instalação. Não há prova que atribua exclusivamente a ele o crash anterior.

## Pendências antes da aceitação integral do observador e recomendações para C

1. Confirmar no retail a build com o resultado booleano corrigido. Ela não foi a
   DLL usada na confirmação funcional acima. Preservar checkpoint 2 e a geração
   existentes; não começar uma campanha nova para esconder uma regressão de Continue.
2. Localizar a recusa tardia de provider_identity. Instrumentar também os ramos de
   revalidação/inicialização do provedor que ainda terminam no evento genérico;
   correlacionar caller, manager, remote e estágio de teardown. Não ampliar a
   exceção de encerramento só porque um checkpoint passou ou não houve NOTICE.
3. Tornar retries de observação idempotentes: IDs de tentativa para descoberta,
   proteção, handoff e export; histórico imutável; tratamento distinto para
   timeout sem processo, interrupção com processo e campanha realmente recusada.
   As recuperações fb16 foram pontuais e não oferecem essa garantia geral.
4. Definir formalmente o escopo de C antes de implementá-lo. Recomendações: integração
   com sala/AP real, reconexão e identidade de slot/opções; distinguir isso do
   provenance synthetic-fixture usado neste marco. Não alegar que room management
   ou a integração completa Archipelago já foi aceita por estes testes.
5. Tratar Cloud e Steam já aberto como trabalho separado, com concorrência de
   escritores, sincronização/conflitos, leases e rollback bem definidos. Não
   desligar a rede inteira nem misturar saves vanilla/AP para contornar o problema.
6. Expandir a matriz retail: mais checkpoints, retorno ao menu, Exit Game, novo
   processo, PRIMARY/BACKUP e recuperação após interrupção. Preservar sempre
   autenticidade nativa, tipos de objetos e falhas reais; evitar fixtures que
   simplesmente repetem suposições não verificadas sobre o engine.

Os logs ajudaram concretamente a separar cada fronteira e a provar que correções
anteriores funcionaram. Também expuseram seus limites: não recuperam um caller que
nunca foi registrado e não garantem flush após falha abrupta de processo ou do SO.
