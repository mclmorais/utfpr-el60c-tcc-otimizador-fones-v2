# Cadeia fixa de módulos, que não muda o volume

Os módulos ficam numa lista definida em tempo de compilação, numa ordem fixa, e cada um tem uma chave de liga e desliga. Rejeitamos uma Cadeia que o usuário pudesse reordenar, porque nenhum módulo planejado precisa disso e ela traria mais telas e mais dados na flash. Cada módulo compensa o próprio ganho: um sinal mono sai com o mesmo volume com o módulo ligado ou desligado. Sem essa regra, ligar um módulo deixa o som mais alto, e o ouvido confunde volume com qualidade, o que invalida a comparação entre ligado e desligado.
