# Log Quality Follower
O projeto denominado como 'Log Quality Follower' tem como objetivo realizar o rastreio de cargas nos mais diversos locais com a utilização de sinal GPS que deverá informar a localidade, data e horario, alem desses o sistema tambem poderá entregar informações da qualidade da viagem como o nivel de vibração pela qual o material transportado está sendo submetido durante seu trejeto.

A principio o projeto conta com desenvolvimento baseado na placa DEMO LilyGO-T-SIM7000g, a qual é descrevida no item referente ao hardware e tambem pode ser melhor estudade através do seu repositorio proprio:
https://github.com/Xinyuan-LilyGO/LilyGO-T-SIM7000G

### Hardware Describe
O projeto foi desenvolvido com base em uma DEMO Board de ESP32 chamada de T-SIM7000g (2020 04 15)
![What is this](T-SIM7000G-2021.jgp)
A placa possui como principais chips:
    ESP32-WROVER-B
    SIM7070g

O hardware tambem possui controle de carga de bateria por sistema solar
O chip responsavel pelo controle de carga de bateria é:
    DW01FA

A placa possui:
    Um dock para baterias 18650;
    Uma entrada USB tipo C;
    Conector para Antena LTE;
    Conector para antena GPS;
    Dock para MicroSD Card

### Configure the project
