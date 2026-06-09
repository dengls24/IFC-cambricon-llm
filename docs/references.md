# References

This repository uses public papers and standards as method references. It does not redistribute the Cambricon-LLM paper PDF, the authors' private SSDsim fork, or RTL/source artifacts from the original work.

## Primary Paper

- Zhongkai Yu, Shengwen Liang, Tianyun Ma, Yunke Cai, Ziyuan Nan, Di Huang, Xinkai Song, Yifan Hao, Jie Zhang, Tian Zhi, Yongwei Zhao, Zidong Du, Xing Hu, Qi Guo, and Tianshi Chen. "Cambricon-LLM: A Chiplet-Based Hybrid Architecture for On-Device Inference of 70B LLM." MICRO 2024. arXiv:2409.15654. DOI: 10.48550/arXiv.2409.15654.

This is the paper whose Figure 9 W8A8 decode-speed path is reproduced by the C timing simulator.

## Simulator And Modeling References

- Yang Hu, Hong Jiang, Dan Feng, Lei Tian, Hao Luo, and Shuping Zhang. "Performance Impact and Interplay of SSD Parallelism through Advanced Commands, Allocation Strategy and Data Granularity." ICS 2011.

This is the public SSDsim-related reference used to document the SSDsim-style terminology and event-driven, multi-tiered SSD modeling boundary. This repository implements a clean, narrow C command-stage/event-loop reconstruction for the Cambricon-LLM IFC path; it is not a copy of SSDsim.

- IEEE Computer Society. "IEEE Standard for Standard SystemC Language Reference Manual." IEEE Std 1666-2011. Accellera Systems Initiative SystemC downloads and standards page: https://www.accellera.org/downloads/standards/systemc

The SystemC paths in this repository are audit models: one replay/equivalence checker and one component-level command-cycle model. They are not RTL.

## BibTeX

BibTeX entries are provided in `data/references.bib`.
