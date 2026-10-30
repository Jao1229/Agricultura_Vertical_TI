VERDEVIVO — PROTÓTIPO DE AGRICULTURA VERTICAL

ESTRUTURA
- index.html: página inicial e apresentação do projeto.
- monitoramento.html: painel das duas torres, indicadores, gráficos e tabela.
- comparacao.html: comparação hipotética de consumo de água e produtividade.
- css/style.css: estilos próprios e adaptação para celular.
- js/main.js: menu de navegação para telas pequenas.
- js/monitoramento.js: dados fictícios e gráficos do painel.
- js/comparacao.js: gráficos de comparação.
- supabase-exemplo.js: anotações para futura integração com Supabase.

COMO ABRIR
1. Extraia o arquivo ZIP para uma pasta.
2. Abra index.html no navegador.
3. Use o menu superior para alternar entre as três páginas.

O projeto usa Tailwind CSS via CDN e Chart.js via CDN, então a primeira abertura
precisa de conexão com a internet para carregar essas bibliotecas.

IMPORTANTE
- Os números são fictícios e servem apenas para testar o layout.
- A página de comparação é uma simulação, não uma conclusão científica.
- A integração com Supabase ainda não está ativa.
- Para publicar o projeto, configure autenticação/regras de acesso e políticas RLS.
- A frequência de 30 minutos é o intervalo planejado para a coleta, que deverá
  ser implementada no dispositivo ou serviço responsável pelos sensores.
